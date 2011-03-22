/*
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Conf.h"
#include "DBHandler.h"
#include "Job.h"
#include "QMException.h"
#include "Util.h"

#include <string>

#include <stdlib.h>
#include <string.h>
#include <mysql.h>
#include <stdio.h>

#include <uuid/uuid.h>

using namespace std;


/* The order here must match the definition of JobStatus */
static const char *status_str[] =
{
	"PREPARE", "INIT", "RUNNING", "FINISHED", "ERROR", "TEMPFAILED", "CANCEL"
};

static vector<DBHandler *> used_dbhs;
static vector<DBHandler *> free_dbhs;
static GMutex *db_lock;
static GCond *db_signal;

/* Database configuration items */
static unsigned max_connections;
static char *dbname;
static char *host;
static char *user;
static char *passwd;

/**********************************************************************
 * Class: DBResult
 */

DBResult::~DBResult()
{
	if (res)
		mysql_free_result(res);
}

void DBResult::store() throw()
{
	/* Allow re-use */
	if (res)
		mysql_free_result(res);

	field_num = mysql_field_count(dbh->conn);
	res = mysql_store_result(dbh->conn);
	if (field_num && !res)
		LOG(LOG_ERR, "Failed to fetch results: %s", mysql_error(dbh->conn));
	else
		fields = mysql_fetch_fields(res);
}

void DBResult::use() throw()
{
	/* Allow re-use */
	if (res)
		mysql_free_result(res);

	field_num = mysql_field_count(dbh->conn);
	res = mysql_use_result(dbh->conn);
	if (res)
		fields = mysql_fetch_fields(res);
	else if (field_num)
		LOG(LOG_ERR, "Failed to fetch results: %s", mysql_error(dbh->conn));
}

bool DBResult::fetch() throw()
{
	if (!res)
		return false;

	row = mysql_fetch_row(res);
	if (!row)
	{
		mysql_free_result(res);
		res = NULL;
	}
	return !!row;
}

const char *DBResult::get_field(int index)
{
	if (index >= field_num || !row)
		throw new QMException("Invalid field index");
	return row[index];
}

const char *DBResult::get_field(const char *name)
{
	int i;

	for (i = 0; i < field_num && strcmp(fields[i].name, name); i++)
		/* Nothing */;
	if (i >= field_num)
		throw new QMException("Unknown field requested: %s", name);
	return row[i];
}

/**********************************************************************
 * Class: DBHandler
 */

bool DBHandler::query(const char *fmt, ...)
{
	va_list ap;
	char *qstr;

	if (!conn)
		throw new QMException("Not connected to the database");

	va_start(ap, fmt);
	vasprintf(&qstr, fmt, ap);
	va_end(ap);

	if (mysql_query(conn, qstr))
	{
		LOG(LOG_ERR, "Query [%s] has failed: %s", qstr, mysql_error(conn));
		free(qstr);
		return false;
	}
	free(qstr);
	return true;
}


void DBHandler::connect()
{
	conn = mysql_init(0);
	if (!conn)
		throw new QMException("Out of memory");
	if (!mysql_real_connect(conn, host, user, passwd, dbname, 0, 0, 0))
	{
		string error = mysql_error(conn);
		mysql_close(conn);
		conn = 0;
		throw new QMException("Could not connect to the database: %s", error.c_str());
	}
}

void DBHandler::check()
{
	if (!mysql_ping(conn))
		return;

	LOG(LOG_ERR, "Database connection error: %s", mysql_error(conn));
	mysql_close(conn);
	connect();
}

DBHandler::DBHandler()
{
	connect();
}


DBHandler::~DBHandler()
{
	mysql_close(conn);
}


static const char *statToStr(Job::JobStatus stat)
{
	if (stat < 0 || stat > (int)(sizeof(status_str) / sizeof(status_str[0])))
		throw new QMException("Unknown job status value %d", (int)stat);
	return status_str[stat];
}


auto_ptr<Job> DBHandler::parseJob(DBResult &res)
{
	uuid_t uuid;

	const char *alg = res.get_field("alg");
	const char *grid = res.get_field("grid");
	const char *args = res.get_field("args");
	const char *gridid = res.get_field("gridid");
	const char *griddata = res.get_field("griddata");
	const char *id = res.get_field("id");
	const char *stat = res.get_field("status");
	const char *tag = res.get_field("tag");

	if (uuid_parse(id, uuid))
	{
		LOG(LOG_ERR, "Invalid job ID: %s", id);
		DBHandler *dbh = get();
		dbh->query("UPDATE cg_job SET status = '%s' WHERE id = '%s'",
			status_str[Job::ERROR], id);
		put(dbh);
		return auto_ptr<Job>(0);
	}

	unsigned i;
	for (i = 0; i < sizeof(status_str) / sizeof(status_str[0]); i++)
		if (!strcmp(status_str[i], stat))
			break;
	if (i >= sizeof(status_str) / sizeof(status_str[0]))
	{
		LOG(LOG_ERR, "Unknown job status: %s", stat);
		return auto_ptr<Job>(0);
	}

	// Create new job descriptor
	auto_ptr<Job> job(new Job(id, alg, grid, args, (Job::JobStatus)i));
	if (gridid)
		job->setGridId(gridid);
	if (griddata)
		job->setGridData(griddata);
	if (tag)
		job->setTag(tag);

	// Get inputs for job from db
	DBHandler *dbh = get();
	if (!dbh->query("SELECT localname, url, md5, filesize FROM cg_inputs WHERE id = '%s'", id))
	{
		job.reset(0);
		put(dbh);
		return job;
	}

	DBResult res2(dbh);
	res2.use();
	while (res2.fetch())
	{
		const char *lname = res2.get_field(0);
		const char *url = res2.get_field(1);
		const char *md5 = res2.get_field(2);
		const char *size = res2.get_field(3);
		FileRef fref(url, md5 ? md5 : "", size ? atoi(size) : -1);
		job->addInput(lname, fref);
	}

	// Get outputs for job from db
	if (!dbh->query("SELECT localname, path FROM cg_outputs WHERE id = '%s'", id))
	{
		job.reset(0);
		put(dbh);
		return job;
	}

	res2.use();
	while (res2.fetch())
		job->addOutput(res2.get_field(0), res2.get_field(1));

	// Get environment variables fro job from db
	if (!dbh->query("SELECT name, val FROM cg_env WHERE id = '%s'", id))
	{
		job.reset(0);
		put(dbh);
		return job;
	}
	res2.use();
	while (res2.fetch())
		job->addEnv(res2.get_field(0), res2.get_field(1));

	put(dbh);
	return job;
}


void DBHandler::parseJobs(JobVector &jobs)
{
	DBResult res(this);

	res.use();
	while (res.fetch())
	{
		auto_ptr<Job> job = parseJob(res);
		if (!job.get())
			continue;
		jobs.push_back(job.get());
		job.release();
	}
}


auto_ptr<Job> DBHandler::getJob(const string &id)
{
	auto_ptr<Job> job(0);

	if (query("SELECT * FROM cg_job WHERE id = '%s'", id.c_str()))
	{
		DBResult res(this);

		res.store();
		if (res.fetch())
			job = parseJob(res);
	}
	return job;
}


void DBHandler::getJobs(JobVector &jobs, const string &grid, const string &alg, Job::JobStatus stat, unsigned batch)
{
	if (query("SELECT * FROM cg_job "
			"WHERE grid = '%s' AND alg = '%s' AND status = '%s' "
			"ORDER BY creation_time LIMIT %d",
			grid.c_str(), alg.c_str(), statToStr(stat), batch))
		return parseJobs(jobs);
}


void DBHandler::getJobs(JobVector &jobs, const string &grid, Job::JobStatus stat, unsigned batch)
{
	if (query("SELECT * FROM cg_job "
			"WHERE grid = '%s' AND status = '%s' "
			"ORDER BY creation_time LIMIT %d",
			grid.c_str(), statToStr(stat), batch))
		return parseJobs(jobs);
}


void DBHandler::getFinishedJobs(JobVector &jobs, const string &grid, unsigned batch)
{
	if (query("SELECT * FROM cg_job "
			"WHERE grid = '%s' AND (status = 'FINISHED' OR status = 'ERROR') "
			"ORDER BY creation_time LIMIT %d",
			grid.c_str(), batch))
		return parseJobs(jobs);
}


void DBHandler::pollJobs(GridHandler *handler, Job::JobStatus stat1, Job::JobStatus stat2)
{
	if (!query("START TRANSACTION"))
		return;
	if (!query("SELECT * FROM cg_job WHERE grid = '%s' "
			"AND (status = '%s' OR status = '%s')",
			handler->getName(), statToStr(stat1), statToStr(stat2)))
	{
		query("ROLLBACK");
		return;
	}

	DBResult res(this);
	res.use();
	while (res.fetch())
	{
		auto_ptr<Job> job(parseJob(res));
		try {
			handler->poll(job.get());
		}
		catch (BackendException *e) {
			LOG(LOG_ERR, "Polling job %s: %s", job->getId().c_str(), e->what());
			delete e;
		}
	}
	query("COMMIT");
}


void DBHandler::getJobs(JobVector &jobs, const char *gridID)
{
	if (query("SELECT * FROM cg_job WHERE gridid = '%s'", gridID))
		return parseJobs(jobs);
}


void DBHandler::loadAlgQStats(void)
{
	DBResult res(this);

	if (!query("SELECT grid, alg, batchsize, statistics FROM cg_algqueue"))
		return;
	res.store();
	while (res.fetch())
	{
		string statStr = res.get_field(3) ? res.get_field(3) : "";
		int size = atoi(res.get_field(2));
		new AlgQueue(res.get_field(0), res.get_field(1), size, statStr);
	}
}


void DBHandler::updateAlgQStat(AlgQueue *algQ, unsigned pSize, unsigned pTime)
{
	algQ->updateStat(pSize, pTime);

	query("UPDATE cg_algqueue SET statistics='%s' WHERE grid='%s' AND alg='%s'",
		algQ->getStatStr().c_str(), algQ->getGrid().c_str(), algQ->getName().c_str());
}


void DBHandler::updateAlgQStat(const char *gridId, unsigned pSize, unsigned pTime)
{
	JobVector jobs;
	getJobs(jobs, gridId);
	AlgQueue *algQ = jobs.at(0)->getAlgQueue();
	updateAlgQStat(algQ, pSize, pTime);
}


void DBHandler::updateJobGridID(const string &ID, const string &gridID)
{
	char *gridid = escape_string(gridID.c_str());
	query("UPDATE cg_job SET gridid='%s' WHERE id='%s'", gridid, ID.c_str());
	free(gridid);
}


void DBHandler::updateJobGridData(const string &ID, const string &gridData)
{
	char *griddata = escape_string(gridData.c_str());
	query("UPDATE cg_job SET griddata='%s' WHERE id='%s'", griddata, ID.c_str());
	free(griddata);
}


void DBHandler::updateJobTag(const string &ID, const string &tag)
{
	char *ctag = escape_string(tag.c_str());
	query("UPDATE cg_job SET tag='%s' WHERE id='%s'", ctag, ID.c_str());
	free(ctag);
}


void DBHandler::updateJobStat(const string &ID, Job::JobStatus newstat)
{
	query("UPDATE cg_job SET status='%s' WHERE id='%s'", statToStr(newstat), ID.c_str());
}


void DBHandler::deleteJob(const string &ID)
{
	query("DELETE FROM cg_job WHERE id='%s'", ID.c_str());
}


void DBHandler::deleteBatch(const string &gridId)
{
	query("DELETE FROM cg_job WHERE gridid = '%s'", gridId.c_str());
}


bool DBHandler::addJob(Job &job)
{
	bool success = true;

	if (!query("START TRANSACTION"))
		return false;

	char *args = escape_string(job.getArgs().c_str());
	char *name = escape_string(job.getName().c_str());
	if (job.getTag())
	{
		char *tag = escape_string(job.getTag()->c_str());
		success &= query("INSERT INTO cg_job (id, alg, grid, status, args, tag) VALUES ('%s', '%s', '%s', '%s', '%s', '%s')",
			job.getId().c_str(), name, job.getGrid().c_str(), statToStr(job.getStatus()), args, tag);
		free(tag);
	}
	else
	{
		success &= query("INSERT INTO cg_job (id, alg, grid, status, args) VALUES ('%s', '%s', '%s', '%s', '%s')",
			job.getId().c_str(), name, job.getGrid().c_str(), statToStr(job.getStatus()), args);
	}
	free(name);
	free(args);

	auto_ptr< vector<string> > files = job.getInputs();
	for (vector<string>::const_iterator it = files->begin(); it != files->end(); it++)
	{
		string path = job.getInputRef(*it).getURL();
		FileRef fr = job.getInputRef(*it);
		char *url = escape_string(fr.getURL().c_str());
		char *md5 = escape_string(fr.getMD5().c_str());
		success &= query("INSERT INTO cg_inputs (id, localname, url, md5, filesize) VALUES ('%s', '%s', '%s', '%s', '%ld')",
			job.getId().c_str(), it->c_str(), url, md5, fr.getSize());
		free(md5);
		free(url);
	}
	files = job.getOutputs();
	for (vector<string>::const_iterator it = files->begin(); it != files->end(); it++)
	{
		string path = job.getOutputPath(*it);
		success &= query("INSERT INTO cg_outputs (id, localname, path) VALUES ('%s', '%s', '%s')",
			job.getId().c_str(), it->c_str(), path.c_str());
	}

	auto_ptr< vector<string> > env = job.getEnvs();
	for (vector<string>::const_iterator it = env->begin(); it != env->end(); it++)
	{
		string val = job.getEnv(*it);
		char *envn = escape_string(it->c_str());
		char *envv = escape_string(val.c_str());
		success &= query("INSERT INTO cg_env (id, name, val) VALUES ('%s', '%s', '%s')",
			job.getId().c_str(), envn, envv);
		free(envv);
		free(envn);
	}

	if (success)
		query("COMMIT");
	else
		query("ROLLBACK");

	return success;
}

static void db_thread_cleanup(void *ptr G_GNUC_UNUSED)
{
	mysql_thread_end();
}

DBHandler *DBHandler::get() throw (QMException *)
{
	static GStaticPrivate db_used_key = G_STATIC_PRIVATE_INIT;

	/* If a thread has used MySQL then we must call mysql_thread_end() when
	 * the thread exits or mysql_library_end() will complain and sleep a
	 * lot */
	if (g_thread_supported() && !g_static_private_get(&db_used_key))
	{
		mysql_thread_init();
		g_static_private_set(&db_used_key, (void *)~0, db_thread_cleanup);
	}

	DBHandler *dbh = 0;

	if (!dbname)
		throw new QMException("The database system has not been initialized");

	if (db_lock)
		g_mutex_lock(db_lock);

retry:
	if (!free_dbhs.empty())
	{
		dbh = free_dbhs.front();
		free_dbhs.erase(free_dbhs.begin());
		goto out;
	}

	if (max_connections && used_dbhs.size() >= max_connections)
	{
		if (db_lock)
		{
			LOG(LOG_DEBUG, "Sleeping while waiting for a DB connection");
			g_cond_wait(db_signal, db_lock);
			goto retry;
		}
		throw new QMException("Too many database connections are open");
	}

	try
	{
		dbh = new DBHandler();
	}
	catch (...)
	{
		if (db_lock)
			g_mutex_unlock(db_lock);
		throw;
	}

out:
	used_dbhs.push_back(dbh);
	if (db_lock)
		g_mutex_unlock(db_lock);

	dbh->check();
	return dbh;
}


void DBHandler::put(DBHandler *dbh)
{
	vector<DBHandler *>::iterator it;

	if (db_lock)
		g_mutex_lock(db_lock);

	for (it = used_dbhs.begin(); it != used_dbhs.end(); it++)
		if (*it == dbh)
			break;
	if (it == used_dbhs.end())
		LOG(LOG_ERR, "Tried to put() a dbh that is not on the used list");
	else
		used_dbhs.erase(it);
	free_dbhs.push_back(dbh);
	if (db_lock)
	{
		g_cond_signal(db_signal);
		g_mutex_unlock(db_lock);
	}
}


void DBHandler::addAlgQ(const char *grid, const char *alg, unsigned batchsize)
{
	query("INSERT INTO cg_algqueue (grid, alg, batchsize, statistics) VALUES('%s', '%s', '%u', '')",
		grid, alg, batchsize);
}


void DBHandler::getCompleteWUs(vector<string> &ids, const string &grid, Job::JobStatus stat)
{
	if (!query("SELECT gridid, COUNT(*) AS total, COUNT(NULLIF(FALSE, status = '%s')) AS matching "
			"FROM cg_job "
			"WHERE grid = '%s' "
			"GROUP BY gridid "
			"HAVING total = matching "
			"LIMIT 100",
			statToStr(stat), grid.c_str()))
		return;

	DBResult res(this);
	res.use();
	while (res.fetch())
		ids.push_back(res.get_field(0));

}


void DBHandler::getCompleteWUsSingle(vector<string> &ids, const string &grid, Job::JobStatus stat)
{
	if (!query("SELECT gridid "
			"FROM cg_job "
			"WHERE grid = '%s' AND status = '%s' "
			"LIMIT 100",
			grid.c_str(), statToStr(stat)))
		return;

	DBResult res(this);
	res.use();
	while (res.fetch())
		ids.push_back(res.get_field(0));
}


void DBHandler::addDL(const string &jobid, const string &localName, const string &url)
{
	query("INSERT INTO cg_download (jobid, localname, url) VALUES ('%s', '%s', '%s')",
		jobid.c_str(), localName.c_str(), url.c_str());
}

void DBHandler::deleteDL(const string &jobid, const string &localName)
{
	query("DELETE FROM cg_download WHERE jobid = '%s' AND localname = '%s'",
		jobid.c_str(), localName.c_str());
}


void DBHandler::updateDL(const string &jobid, const string &localName, const struct timeval &next,
		int retries)
{
	char timebuf[20];
	struct tm tm;
	time_t t;

	t = next.tv_sec;
	localtime_r(&t, &tm);
	strftime(timebuf, sizeof(timebuf), "%F %T", &tm);

	query("UPDATE cg_download "
		"SET next_try = '%s', retries = %d "
		"WHERE jobid = '%s' AND localname = '%s'",
		timebuf, retries, jobid.c_str(), localName.c_str());
}


void DBHandler::getAllDLs(void (*cb)(const char *jobid, const char *localName,
		const char *url, const struct timeval *next, int retries))
{
	if (!query("SELECT jobid, localname, url, UNIX_TIMESTAMP(next_try), retries "
			"FROM cg_download"))
		return;

	struct timeval next;
	next.tv_usec = 0;

	DBResult res(this);
	res.use();
	while (res.fetch())
	{
		const char *jobid = res.get_field(0);
		const char *localName = res.get_field(1);
		const char *url = res.get_field(2);
		const char *next_str = res.get_field(3);
		const char *retries_str = res.get_field(4);

		next.tv_sec = atol(next_str);
		cb(jobid, localName, url, &next, atoi(retries_str));
	}
}


void DBHandler::updateInputPath(const string &jobid, const string &localName,
		const string &path)
{
	char *url = escape_string(path.c_str());
	query("UPDATE cg_inputs "
		"SET url = '%s' "
		"WHERE id = '%s' AND localname = '%s'",
		url, jobid.c_str(), localName.c_str());
	free(url);
}

void DBHandler::init(GKeyFile *config)
{
	GError *error = NULL;

	/* The database name is mandatory. Here we leak the GError but this is
	 * a non-recoverable error so... */
	dbname = g_key_file_get_string(config, GROUP_DATABASE, "name", &error);
	if (error)
		throw new QMException("Failed to retrieve the database name: %s", error->message);
	if (!dbname || !strlen(dbname))
		throw new QMException("The database name is not specified in the configuration file");
	g_strstrip(dbname);

	/* These are not mandatory */
	host = g_key_file_get_string(config, GROUP_DATABASE, "host", NULL);
	user = g_key_file_get_string(config, GROUP_DATABASE, "user", NULL);
	passwd = g_key_file_get_string(config, GROUP_DATABASE, "password", NULL);
	if (host)
		g_strstrip(host);
	if (user)
		g_strstrip(user);

	/* max-connections is not mandatory, but if it is present it must be valid */
	max_connections = g_key_file_get_integer(config, GROUP_DATABASE, "max-connections", &error);
	if (error)
	{
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
			LOG(LOG_ERR, "Failed to parse the max DB connection number: %s", error->message);
		g_error_free(error);
	}

	/* When using multiple threads this must be called explicitely
	 * before any threads are launched */
	mysql_library_init(0, NULL, NULL);
	if (g_thread_supported())
	{
		db_lock = g_mutex_new();
		db_signal = g_cond_new();
	}
}

void DBHandler::done()
{
	/* If used_dbhs is not empty, we have a leak somewhere */
	if (!used_dbhs.empty())
		LOG(LOG_WARNING, "There are database handles in use on shutdown");

	while (!free_dbhs.empty())
	{
		DBHandler *dbh = free_dbhs.front();
		free_dbhs.erase(free_dbhs.begin());
		delete dbh;
	}

	g_free(dbname);
	g_free(host);
	g_free(user);
	g_free(passwd);
	mysql_library_end();

	if (db_lock)
	{
		g_mutex_free(db_lock);
		g_cond_free(db_signal);
	}
}

char *DBHandler::escape_string(const char *input)
{
	if (input == NULL)
		return NULL;

	int inlen = strlen(input);
	char *dbuf = (char *)malloc(2*inlen+1);
	mysql_real_escape_string(conn, dbuf, input, inlen);
	return dbuf;
}
