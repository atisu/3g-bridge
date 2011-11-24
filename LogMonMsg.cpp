/** -*- mode: c++; coding: utf-8-unix -*-
 *
 * Copyright (C) 2009-2010 MTA SZTAKI LPDS
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * In addition, as a special exception, MTA SZTAKI gives you permission to link
 * the code of its release of the 3G Bridge with the OpenSSL project's "OpenSSL"

 * library (or with modified versions of it that use the same license as the
 * "OpenSSL" library), and distribute the linked executables. You must obey the
 * GNU General Public License in all respects for all of the code used other
 * than "OpenSSL". If you modify this file, you may extend this exception to
 * your version of the file, but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version.
 */

#include "LogMonMsg.h"
#include <glib/gstdio.h>
#include <memory>
#include <fstream>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "mkstr"
#include "Conf.h"
#include "QMException.h"
#include "Util.h"

using namespace std;
using namespace logmon;

// Placeholder for timestamp at the beginning of the temporary log file. This
// will be changed to the actual value using mmap() when rotating the log file.
#define TS_LEN 15
const string TS_PLACEHOLDER(TS_LEN, '?');

// Configuration keys
static CSTR_C CFG_DEFAULT_GROUP = GROUP_DEFAULTS;
static CSTR_C CFG_LOGFILENAME = "monlog-target";
static CSTR_C CFG_BUILDERTYPE = "monlog-format";
static CSTR_C CFG_TIMEZONE = "monlog-timezone";
static CSTR_C CFG_ROTATE_IV = "monlog-rotate-interval";
static CSTR_C CFG_ROTATE_FN = "monlog-rotate-filenamefmt";

// Static members
LogMon *LogMon::_instance = 0;
const XMLBuilder::KTMapping XMLBuilder::key_tag_mapping;

static timestamp_type getnow()
{
	timeval t;
	gettimeofday(&t, 0);
	return t.tv_sec * 1000 + t.tv_usec/1000;
}
/// Convert minutes to timestamp_type.
static timestamp_type min2ts(int minutes)
{
	return (timestamp_type)(minutes*60*1000);
}
/// Convert timestamp_type to usecond.
static long ts2usec(timestamp_type ts)
{
	return (long)(ts/60*1000);
}

LogMon &LogMon::instance(GKeyFile *conf, CSTR group)
{
	if (!_instance)
	{
		LOG(LOG_DEBUG, "[LogMon] Creating instance");
		// conf may be NULL iff _instance!=NULL
		if (!conf)
		{
			throw new QMException("Tried to create LogMon instance "
					      "without configuration.");
		}

		// Default group
		if (!group)
			group = CFG_DEFAULT_GROUP;

		const string &buildertype =
			config::getConfStr(conf, group,
					   CFG_BUILDERTYPE, "simple");
		const string &logfilename =
			config::getConfStr(conf, group, CFG_LOGFILENAME, "");
		const string &timezone =
			config::getConfStr(conf, group, CFG_TIMEZONE, "GMT");
		int rotint_min = config::getConfInt(conf, group,
						    CFG_ROTATE_IV, 0);
		if (rotint_min < 0)
			throw new QMException(
				"Invalid rotation interval has been specified.");
		
		timestamp_type rotateInterval = min2ts(rotint_min);
		const string &rotateFilenameFmt =
			config::getConfStr(conf, group, CFG_ROTATE_FN,
					   (logfilename + "-{ts}").c_str());

		LOG(LOG_DEBUG, "[LogMon] config: '%s'->'%s' (%s) %s @%dmin",
		    logfilename.c_str(),
		    rotateFilenameFmt.c_str(),
		    buildertype.c_str(),
		    timezone.c_str(),
		    rotint_min);

		_instance = new LogMon(Builder::create(buildertype),
				       group, logfilename, timezone,
				       rotateInterval, rotateFilenameFmt);
	}

	return *_instance;
}

LogMon::LogMon(Builder *builder,
	       const string &name,
	       const string &logfilename,
	       const string &timezone,
	       timestamp_type rotateInterval,
	       const string &rotateFilenameFmt)
	: _builder(builder),
	  _name(name),
	  _logfilename(logfilename),
	  _timezone(timezone),
	  _rotateFilenameFmt(rotateFilenameFmt),
	  _rotateInterval(rotateInterval)
{
	FileLock::init(*this);
}
LogMon::~LogMon()
{
	delete _builder;
	_instance = 0;
	FileLock::destroy();
}

Message LogMon::createMessage()
{
	if (_logfilename.empty())
		return DummyMessage(*_builder);
	else
		return Message(*_builder);
}
void LogMon::logrotate()
{
	if (!_rotateInterval)
		return;
	
	FileLock lock = lockFile();

	timestamp_type now = getnow();
	char now_s[16];
	sprintf(now_s, "%lu", now);

	string filename = _rotateFilenameFmt;
	filename.replace(filename.find("{ts}"), 4, now_s);
	if (filename[0] != '/')
	{
		auto_ptr<gchar> dirname(g_path_get_dirname(
						_logfilename.c_str()));
		auto_ptr<gchar> newname(g_build_filename(dirname.get(),
							 filename.c_str(),
							 NULL));
		filename = newname.get();
	}

	// If the temporary log file is empty, the periodic snapshot must still
	// be created.
	if (!g_file_test(_logfilename.c_str(), G_FILE_TEST_EXISTS))
		_builder->beginFile(now);
	// Close existing temporary log file.
	_builder->endFile(now);

	// Rotate the log
	g_rename(_logfilename.c_str(), filename.c_str());

	LOG(LOG_DEBUG, "[LogMon] Rotated log file '%s' -> '%s'",
	    _logfilename.c_str(), filename.c_str());
}

LogMon::FileLock LogMon::lockFile() const
{
	return FileLock(*this);
}

/****************************************************************
 *  Message
 ****************************************************************/

Message::Message(Builder &builder)
	: _builder(builder)
{
	time_t now = time(0);
	struct tm t;
	char timebuf[34];
	localtime_r(&now, &t);
	strftime(timebuf, sizeof(timebuf), "%F_%T", &t);

	_items.push_back(KVP("dt", timebuf));
}
void Message::save()
{
	_builder.saveMessage(*this);
}

/****************************************************************
 *  Builder
 ****************************************************************/
Builder *Builder::create(const string &buildertype)
{
	if (buildertype.empty() || buildertype == "simple")
		return new SimpleBuilder();
	else if (buildertype == "xml")
		return new XMLBuilder();
	else
		throw new QMException("Unknown monlog-format specified");

}
LogMon &Builder::parent() const
{
	return LogMon::instance(0);
}
void Builder::saveMessage(const Message &msg)
{
	const LogMon &p = parent();

	LogMon::FileLock lock = p.lockFile();

	// Create and initialize file if empty
	if (!g_file_test(p._logfilename.c_str(), G_FILE_TEST_EXISTS))
		beginFile(getnow());
	// Add message
	ofstream of(p._logfilename.c_str(), ios::app);
	of << message(msg._items);
}

/****************************************************************
 *  XMLBuilder
 ****************************************************************/

XMLBuilder::KTMapping::KTMapping()
{
	_mapping["dt"]="dt";
	_mapping["event"]="event";
	_mapping["job_id"]="job_id";
	_mapping["application"]="application";
	_mapping["status"]="status";
	_mapping["output_grid_name"]="output_grid_name";
}
string XMLBuilder::KTMapping::operator[](const string &key) const
{
	mapping_type::const_iterator k = _mapping.find(key);
	return k!=_mapping.end() ? k->second : "";
}

void XMLBuilder::beginFile(const timestamp_type &)
{
	// !! Assuming critical section

	const LogMon &p = parent();

	// Create header
	const string & startLine = MKStr()
		<< "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << endl
		<< "<report timestamp=" << TS_PLACEHOLDER << " timezone=\""
		<< p.timezone() << "\" version=\"1.1\">" << endl;

	// Truncate/create file; add header
	GError *err = 0;
	if (!g_file_set_contents(p.logfilename().c_str(),
				 startLine.c_str(),
				 startLine.length(), &err))
	{
		QMException *ex = new QMException(
			"Starting a new mon log file has failed: %s",
			err->message);
		g_error_free(err);
		throw ex;
	}
}
void XMLBuilder::endFile(const timestamp_type &now)
{
	// !! Assuming critical section

	const LogMon &p = parent();
	CSTR_C filename = p.logfilename().c_str();

	// Add closing tag
	{
		ofstream of(filename, ios::app);
		of << "</report>" << endl;
	}

	// Change timestamp

	char timestamp[TS_LEN+1], formattedTimestamp[TS_LEN+1];
	sprintf(timestamp, "\"%lu\"", now);
	// Padding right with spaces to overwrite whole placeholder
	sprintf(formattedTimestamp, "%-*s", TS_LEN, timestamp);

	//Map first page of the file to modify timestamp
	// !! Assuming placeholder is in first page (if exists)
	int fd = open(filename, O_RDWR);
	int pagesize = getpagesize();
	char *contents = (char*)mmap(0, pagesize, PROT_READ|PROT_WRITE,
				     MAP_SHARED, fd, 0);
	char* ts_start=g_strstr_len(contents, pagesize, TS_PLACEHOLDER.c_str());
	if (ts_start)
	{
		// There must not be a terminating \0
		//  --> memcpy instead of strncpy
		memcpy(ts_start, formattedTimestamp, TS_LEN);
	}
	munmap(contents, pagesize);
	close(fd);
}
string XMLBuilder::message(const KVPList &items)
{
	MKStr sb;
	sb << "  <metric_data>" << endl;
	for (KVPList::const_iterator i = items.begin();
	     i != items.end(); i++)
	{
		// Keys are mapped and maybe omitted
		const string &key = key_tag_mapping[i->first];
		if (!key.empty())
		{
			sb << "    <" << key << ">"
			   << i->second
			   << "</" << key << ">" << endl;
		}
	}
	sb << "  </metric_data>" << endl;
	return sb;
}

/****************************************************************
 *  SimpleBuilder
 ****************************************************************/

string SimpleBuilder::message(const KVPList &items)
{
	MKStr sb;
	bool first = true;
	for (KVPList::const_iterator i = items.begin();
	     i != items.end(); i++)
	{
		if (first) first=false;
		else sb << " ";
		sb << i->first << "=" << i->second;
	}
	sb << endl;
	return sb;
}

/****************************************************************
 *  Rotation thread
 ****************************************************************/

//Rotate thread
static GThread *_rotate_th = 0;
// Condition to exit from log rotate thread
static GCond *exitCondition = 0;

#define SET_NEXT							\
	do {								\
		g_time_val_add(&timeval,				\
			       ts2usec(parent->rotateInterval()));	\
	} while (false)
static gpointer rotate_thread_func(gpointer data)
try
{
	LOG(LOG_DEBUG, "[LogMon:Rot] Thread started.");
	LogMon *parent = reinterpret_cast<LogMon*>(data);
	GTimeVal timeval;
	GMutex *mut = g_mutex_new(); //needed by g_cond*_wait
	g_mutex_lock(mut);

	g_get_current_time(&timeval);
	// Create startup file -- empty or from temp log created last time
	parent->logrotate();
	SET_NEXT;

	for (;;)
	{
		if (g_cond_timed_wait(exitCondition, mut, &timeval))
			break;

		LOG(LOG_DEBUG, "[LogMon:Rot] Rotating mon log file...");
		parent->logrotate();
		SET_NEXT;
	}

	g_mutex_unlock(mut);
	g_mutex_free(mut);

	LOG(LOG_DEBUG, "[LogMon:Rot] Thread done.");
	return 0;
}
catch (const exception &ex)
{
	LOG(LOG_CRIT, "[LogMon:Rot] Unhandled exception: %s", ex.what());
	LOG(LOG_CRIT, "[LogMon:Rot] Log-rotate thread terminated.");
}
catch (const exception *ex)
{
	LOG(LOG_CRIT, "[LogMon:Rot] Unhandled exception: %s", ex->what());
	LOG(LOG_CRIT, "[LogMon:Rot] Log-rotate thread terminated.");
}
void logmon::startRotationThread(GKeyFile *conf, CSTR group)
{
	if (_rotate_th)
		throw new QMException("[LogMon] Rotation thread "
				      "has already been started");
	GError *err;
	LogMon &lm = LogMon::instance(conf, group);
	if (lm.rotateInterval())
	{
		exitCondition = g_cond_new();
		if (!(lm.logfilename().empty()))
		{
			if (!(_rotate_th = g_thread_create(rotate_thread_func,
							   &lm, true, &err)))
			{
				QMException *ex = new QMException(
					"Couldn't start thread for "
					"log rotation: %s",
					err->message);
				g_error_free(err);
				throw ex;
			}
		}
	}
}
void logmon::endRotationThread()
{
	if (!_rotate_th)
		return;

	LOG(LOG_DEBUG, "[LogMon] Waiting for rotation thread to exit...");
	g_cond_signal(exitCondition);
	g_thread_join(_rotate_th);
	g_cond_free(exitCondition);
}

/****************************************************************
 *  Exclusive locking of temporary log file
 ****************************************************************/

//
// Locking is implemented using POSIX semaphores
//

static string semaphore_name;

/// Semaphore checked-open -- close wrapper (try-finally)
class SemOpen
{
	sem_t *s;
public:
	SemOpen()
	{
		if (!(s = sem_open(semaphore_name.c_str(), 0)))
			throw new QMException(
				"[LogMon] Error opening file-lock semaphore: %s",
				strerror(errno));
	}
	~SemOpen() { sem_close(s); }
	operator sem_t*() { return s; }
};
void LogMon::FileLock::init(const LogMon &parent)
{
	semaphore_name = MKStr() << "/mon-log-lock--" << parent.name();
	
	// Create/overwrite/ semaphore. Actually, mutex: it's initialized to 1.
	sem_t *s;
	if (!(s = sem_open(semaphore_name.c_str(), O_CREAT, 00660, 1)))
	{
		throw new QMException(
			"[LogMon] Error creating file-lock semaphore: %s",
			strerror(errno));
	}
	else
		sem_close(s);
}
void LogMon::FileLock::destroy()
{
	sem_unlink(semaphore_name.c_str());
}
LogMon::FileLock::FileLock(const LogMon &parent)
{
	sem_wait(SemOpen());
}
LogMon::FileLock::~FileLock()
{
	sem_post(SemOpen());
}
