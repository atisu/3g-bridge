/* -*- mode: c++; coding: utf-8-unix -*-
 * Copyright (C) 2008-2010 MTA SZTAKI LPDS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * In addition, as a special exception, MTA SZTAKI gives you permission to link the
 * code of its release of the 3G Bridge with the OpenSSL project's "OpenSSL"
 * library (or with modified versions of it that use the same license as the
 * "OpenSSL" library), and distribute the linked executables. You must obey the
 * GNU General Public License in all respects for all of the code used other than
 * "OpenSSL". If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version.
 */

#include "ForeignProbe.h"
#include "Conf.h"
#include "Mutex.h"
#include "DBHandler.h"
#include "mkstr"
#include <sstream>
#include <fstream>
#include <memory.h>
#include "FP_queries.h"

static void checkGError(CSTR context, GError *error)
{
	if (error != 0)
	{
		QMException *ex =new QMException("glib error: %s: %s",
						 context, error->message);
		g_error_free(error);
		throw ex;
	}
}

// Configuration keys
static CSTR_C key_interval = "reporting_interval";
static CSTR_C key_logdir = "target_filenamefmt";
static CSTR_C key_hostiden = "host_ident";
static CSTR_C key_middleware = "middleware"; //boinc
static CSTR_C key_direction = "direction"; //sgdg
static CSTR_C key_apps = "apps";
static CSTR_C key_maxthreads = "max_threads";

using namespace foreignprobe;
using namespace std;

ForeignProbe::ForeignProbe(GKeyFile *config, CSTR instance)
	: _exit_condition(g_cond_new()),
	  _exit_condition_mx(g_mutex_new()),
	  _main_thread(0),
	  _conf(config, instance) {}
ForeignProbe::~ForeignProbe()
{
	if (_main_thread)
	{
		g_mutex_lock(_exit_condition_mx);
		g_cond_broadcast(_exit_condition);
		g_mutex_unlock(_exit_condition_mx);

		g_thread_join(_main_thread);
	}
	if (_exit_condition) g_cond_free(_exit_condition);
	if (_exit_condition_mx) g_mutex_free(_exit_condition_mx);
}

void ForeignProbe::init(events::EventPool&)
{
	LOG(LOG_INFO, "[ForeignProbe] Initializing main thread");
	GError *error = 0;
	_main_thread = g_thread_create(th_main, this, true, &error);
	checkGError("ForeignProbe::init", error);
}

gpointer ForeignProbe::th_main(gpointer data)
{
	ForeignProbe *probe = reinterpret_cast<ForeignProbe*>(data);

	GTimeVal wait_till;
	bool running = true;

	while (running)
	{
		LOG(LOG_INFO, "[ForeignProbe] Running queries");
			
		timeval t;
		gettimeofday(&t, 0);
		t_timestamp ts = t.tv_sec * 1000 + t.tv_usec/1000;
		char ts_s[16];
		sprintf(ts_s, "%lu", ts);

		string path = probe->_conf.targetpath();
		string::size_type tsp = path.find("{ts}");
		if (tsp == string::npos)
			path = path + ts_s;
		else
			path.replace(tsp, 4, ts_s);

		LOG(LOG_DEBUG, "[ForeignProbe] Creating '%s'", path.c_str());
		
		{
			ofstream os(path.c_str(), ios::trunc|ios::out);
			if (!os.good())
			{
				LOG(LOG_ERR,
				    "[ForeignProbe] Couldn't open "
				    "file '%s' for writing",
				    path.c_str());
			}
			else
			{
				os << probe->metrics(ts);
			}
		}
		
		g_get_current_time(&wait_till);
		g_time_val_add(&wait_till, probe->_conf.interval()*1000000);
		
		g_mutex_lock(probe->_exit_condition_mx);
		running = ! g_cond_timed_wait(probe->_exit_condition,
					      probe->_exit_condition_mx,
					      &wait_till);
		g_mutex_unlock(probe->_exit_condition_mx);
	}

	return 0;
}



/************************************************************************
 * Generating individual metrics
 */

MetricData::MetricData(CR_string name, CR_string type, CR_string unit)
	: _name(name), _type(type), _unit(unit) {}

SimpleMetricData::SimpleMetricData(CR_string name, CR_string type,
				   CR_string unit, CR_string query)
	: MetricData(name, type, unit), _query(query) {}

static CSTR_C tab = "    ";

template <class T>
static void append(MKStr& os, CSTR_C tag, const T &value)
{
	os << tab << '<' << tag << '>' << value
	   << "</" << tag << '>' << endl;
}
const string MetricData::getcurrent(CR_AppDesc app,
				    CR_string direction,
				    t_timestamp ts) const
{
	float value = runquery(app);

	MKStr os;
	os << "<metric_data>" << endl;
	append(os, "metric_name", name());
	append(os, "timestamp", ts);
	append(os, "value", value);
	append(os, "type", type());
	append(os, "units", unit());
	append(os, "spoof", app.spoof);
	append(os, "direction", direction);
	os << "</metric_data>" << endl;
	return os;
}

float SimpleMetricData::runquery(CR_AppDesc app) const
{
	LOG(LOG_DEBUG,
	    "[ForeignProbe] Executing query '%s' for app '%s'",
	    name().c_str(), app.appname.c_str());
	DBHWrapper dbh;
	return dbh->executeScalar_Float(0, query().c_str(), app.appid);
}

/***********************************************************************
 * Configuration
 */

static const string mkspoof(CR_string hostiden, CR_string appname)
{
	return MKStr() << hostiden << '|' << appname << ':'
		       << hostiden << '|' << appname;
}
static const string getDirection(GKeyFile *config, CSTR instance)
{
	CR_string dir = config::getConfStr(config, instance, key_direction, "sgdg");
	CR_string mw = config::getConfStr(config, instance, key_middleware, "boinc");

	return MKStr() << dir << '|' << mw;
}

const map<string, AppDesc>
ForeignProbe::Configuration::getApps(GKeyFile *config, CSTR instance)
{
#define TRIMBUF_MAXLEN 256

	map<string, AppDesc> mapps;

	CR_string hostiden = config::getConfStr(config, instance, key_hostiden);
	CR_string apps = config::getConfStr(config, instance, key_apps, "");

	if (!apps.empty())
	{
		DBHWrapper dbh;
		istringstream is(apps);
		char trimbuf[TRIMBUF_MAXLEN+1];

		string token;
		while (getline(is, token, ','))
			try
			{
				strncpy(trimbuf, token.c_str(), TRIMBUF_MAXLEN);
				g_strstrip(trimbuf);
				CR_string appname = trimbuf;
				mapps[appname]=AppDesc(
					dbh->getBOINCAppId(appname),
					appname,
					mkspoof(hostiden, appname));
			}
			catch (const BOINCAppNotFoundException &ex)
			{
				LOG(LOG_ERR, "%s", ex.what());
			}
	}

	return mapps;
}

const map<string,MetricData*>
ForeignProbe::Configuration::getQueries(GKeyFile *config, CSTR instance)
{
	map<string, MetricData*> queries;

	for (MetricData** i = standardMetrics; *i; i++)
	{
		queries[(*i)->name()] = *i;
		//TODO: possible override in config file
	}

	return queries;
}

AppDesc::AppDesc(int appid, CR_string appname, CR_string spoof)
	: appid(appid), appname(appname), spoof(spoof) {}

ForeignProbe::Configuration::Configuration(GKeyFile *config, CSTR instance)
	: _interval(config::getConfInt(config, instance, key_interval, 600)),
	  _maxthreads(config::getConfInt(config, instance, key_maxthreads, 5)),
	  _direction(getDirection(config, instance)),
	  _targetpath(config::getConfStr(config, instance, key_logdir)),
	  _apps(getApps(config, instance)),
	  _queries(getQueries(config, instance))
{
	MKStr buf;
	buf << "targetfmt='" << targetpath()
	    << "' interval=" << interval()
	    << " maxthreads=" << maxthreads()
	    << " direction='" << direction()
	    << "' apps=";
	bool first = true;
	for (appit a = apps().begin(); a!=apps().end(); a++)
	{
		if (first) first = false;
		else buf << ',';
		buf << a->first << '(' << a->second.appid << ')';
	}
	buf << " queries=";
	first=true;
	for (metrit m = queries().begin(); m!=queries().end(); m++)
	{
		if (first) first = false;
		else buf << ",";
		buf << m->first;
	}
	CR_string s = buf;
	LOG(LOG_DEBUG, "[ForeignProbe] Configuration: %s", s.c_str());
}

/************************************************************************
 * Running metric queries
 */

struct ThreadUserData
{
	CR_string direction;
	const t_timestamp ts;

	ThreadUserData(CR_string direction, const t_timestamp &ts)
		: direction(direction), ts(ts) {}
};
struct ThreadData
{
	const MetricData *metric;
	CR_AppDesc app;
	GMutex *results_mutex;
	list<string> &results;

	ThreadData(const MetricData *metric, CR_AppDesc app,
		   GMutex *results_mutex, list<string> &results)
		: metric(metric), app(app),
		  results_mutex(results_mutex), results(results) {}
};

static void th_runmetrics(gpointer data, gpointer userdata)
{
	ThreadUserData *ud = reinterpret_cast<ThreadUserData*>(userdata);
	ThreadData *d = reinterpret_cast<ThreadData*>(data);

	LOG(LOG_DEBUG, "[ForeignProbe] Thread execute query start");
	CR_string result = d->metric->getcurrent(d->app, ud->direction, ud->ts);
	CriticalSection cs(d->results_mutex);
	d->results.push_back(result);
	LOG(LOG_DEBUG, "[ForeignProbe] Thread execute query done");
}

const string ForeignProbe::metrics(t_timestamp ts) const
{
	MKStr buff;
	buff << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl;
	buff << "<report timestamp=\"" << ts
	     << "\" timezone=\"GMT\" version=\"1.1\">" << endl;

	GError *error = 0;
	ThreadUserData userdata(_conf.direction(), ts);
	GMutex *results_mutex = g_mutex_new();
	list<string> results;
	GThreadPool *pool = g_thread_pool_new(th_runmetrics,
					      &userdata,
					      _conf.maxthreads(),
					      true, &error);
	checkGError("ForeignProbe::thread_pool_new()", error);

	const appmap &apps = _conf.apps();
	const metrmap &metrs = _conf.queries();

	for (metrit m = metrs.begin(); m != metrs.end(); m++)
		for (appit a=apps.begin(); a!=apps.end(); a++)
		{
			g_thread_pool_push(pool,
					   new ThreadData(m->second,
							  a->second,
							  results_mutex,
							  results),
					   &error);
			checkGError("ForeignProbe::thread_pool_push()", error);
		}

	g_thread_pool_free(pool, false, true); //wait all
	g_mutex_free(results_mutex);

	typedef list<string>::const_iterator lsit;
	for (lsit i = results.begin(); i!=results.end(); i++)
		buff << *i;

	buff << "</report>" << endl;
	return buff;
}

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new ForeignProbe(config, instance);
}
