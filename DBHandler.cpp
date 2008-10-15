#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Job.h"
#include "DBHandler.h"
#include "Conf.h"

#include <string>

#include <stdlib.h>
#include <string.h>
#include <mysql.h>

#include "Bridge.h"
#include "Logging.h"
#include "QMException.h"

using namespace std;


static DBPool db_pool;

/* The order here must match the definition of JobStatus */
static const char *status_str[] =
{
	"PREPARE", "INIT", "RUNNING", "FINISHED", "ERROR", "CANCEL"
};

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
	if (field_num && !res)
		LOG(LOG_ERR, "Failed to fetch results: %s", mysql_error(dbh->conn));
	else
		fields = mysql_fetch_fields(res);
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
	if (index >= field_num)
		throw new QMException("Invalid field index");
	return row[index];
}

const char *DBResult::get_field(const char *name)
{
	int i;

	for (i = 0; i < field_num && strcmp(fields[i].name, name); i++)
		/* Nothing */;
	if (i >= field_num)
		throw new QMException("Unknown field requested: %s ", name);
	return row[i];
}

/**********************************************************************
 * Class: DBHandler
 */

bool DBHandler::query(const char *fmt, ...)
{
	va_list ap;
	char *qstr;

	va_start(ap, fmt);
	vasprintf(&qstr, fmt, ap);
	va_end(ap);

	if (mysql_ping(conn))
	{
		LOG(LOG_ERR, "Database connection error: %s", mysql_error(conn));
		free(qstr);
		return false;
	}
	if (mysql_query(conn, qstr))
	{
		LOG(LOG_ERR, "Query [%s] has failed: %s", qstr, mysql_error(conn));
		free(qstr);
		return false;
	}
	free(qstr);
	return true;
}


DBHandler::DBHandler(const char *dbname, const char *host, const char *user, const char *passwd)
{
	conn = mysql_init(0);
	if (!conn)
		throw new QMException("Out of memory");
	if (!mysql_real_connect(conn, host, user, passwd, dbname, 0, 0, 0))
		throw new QMException("Could not connect to the database: %s", mysql_error(conn));
}


DBHandler::~DBHandler()
{
	mysql_close(conn);
}


//static const char *statToStr(Job::JobStatus stat)
const char *statToStr(Job::JobStatus stat)
{
	if (stat < 0 || stat > (int)(sizeof(status_str) / sizeof(status_str[0])))
		throw new QMException("Unknown job status value %d", (int)stat);
	return status_str[stat];
}


static Job::JobStatus statFromStr(const char *stat)
{
	unsigned i;

	for (i = 0; i < sizeof(status_str) / sizeof(status_str[0]); i++)
		if (!strcmp(status_str[i], stat))
			return (Job::JobStatus)i;
	return Job::INIT;
}


Job *DBHandler::parseJob(DBResult &res)
{
	const char *alg = res.get_field("alg");
	const char *grid = res.get_field("grid");
	const char *args = res.get_field("args");
	const char *gridid = res.get_field("gridid");
	const char *id = res.get_field("id");
	const char *stat = res.get_field("status");

	// Create new job descriptor
	Job *job = new Job(id, alg, grid, args, statFromStr(stat));
	if (gridid)
		job->setGridId(gridid);

	// Get inputs for job from db
	DBHandler *dbh = get();
	if (!dbh->query("SELECT localname, path FROM cg_inputs WHERE id = '%s'", id))
	{
		delete job;
		put(dbh);
		return 0;
	}

	DBResult res2(dbh);
	res2.use();
	while (res2.fetch())
		job->addInput(res2.get_field(0), res2.get_field(1));

	// Get outputs for job from db
	if (!dbh->query("SELECT localname, path FROM cg_outputs WHERE id = '%s'", id))
	{
		delete job;
		put(dbh);
		return 0;
	}

	res2.use();
	while (res2.fetch())
		job->addOutput(res2.get_field(0), res2.get_field(1));
	put(dbh);
	return job;
}


void DBHandler::parseJobs(JobVector &jobs)
{
	DBResult res(this);

	res.use();
	while (res.fetch())
	{
		Job *job = parseJob(res);
		if (!job)
			continue;
		jobs.push_back(job);
	}
}


auto_ptr<Job> DBHandler::getJob(const string &id)
{
	JobVector jobs;
	if (query("SELECT * FROM cg_job WHERE id = '%s'", id.c_str()))
	{
		parseJobs(jobs);
		Job *job = jobs.at(0);
		jobs.erase(jobs.begin());
		return auto_ptr<Job>(job);
	}
	return auto_ptr<Job>(0);
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


void DBHandler::pollJobs(GridHandler *handler, Job::JobStatus stat1, Job::JobStatus stat2)
{
	query("START TRANSACTION");
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
		Job *job = parseJob(res);
		try {
			handler->poll(job);
		}
		catch (BackendException *e) {
			LOG(LOG_ERR, "Polling job %s: %s", job->getId().c_str(), e->what());
			delete e;
		}
		delete job;
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
	query("UPDATE cg_job SET gridid='%s' WHERE id='%s'", gridID.c_str(), ID.c_str());
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


void DBHandler::addJob(Job &job)
{
	bool success = true;
	query("START TRANSACTION");

	success &= query("INSERT INTO cg_job (id, alg, grid, status, args) VALUES ('%s', '%s', '%s', '%s', '%s')",
		job.getId().c_str(), job.getName().c_str(), job.getGrid().c_str(),
		statToStr(job.getStatus()), job.getArgs().c_str());

	auto_ptr< vector<string> > files = job.getInputs();
	for (vector<string>::const_iterator it = files->begin(); it != files->end(); it++)
	{
		string path = job.getInputPath(*it);
		success &= query("INSERT INTO cg_inputs (id, localname, path) VALUES ('%s', '%s', '%s')",
			job.getId().c_str(), it->c_str(), path.c_str());
	}
	files = job.getOutputs();
	for (vector<string>::const_iterator it = files->begin(); it != files->end(); it++)
	{
		string path = job.getOutputPath(*it);
		success &= query("INSERT INTO cg_outputs (id, localname, path) VALUES ('%s', '%s', '%s')",
			job.getId().c_str(), it->c_str(), path.c_str());
	}

	if (success)
		query("COMMIT");
	else
		query("ROLLBACK");
}


DBHandler *DBHandler::get() throw (QMException *)
{
	return db_pool.get();
}


void DBHandler::put(DBHandler *dbh)
{
	db_pool.put(dbh);
}


void DBHandler::addAlgQ(const char *grid, const char *alg, unsigned batchsize)
{
	query("INSERT INTO cg_algqueue(grid, alg, batchsize, statistics) VALUES('%s', '%s', '%u', '')",
		grid, alg, batchsize);
}


void DBHandler::getCompleteWUs(vector<string> &ids, const string &grid, Job::JobStatus stat)
{
	query("SELECT gridid, COUNT(*) AS total, COUNT(NULLIF(FALSE, status = '%s')) AS matching "
		"FROM cg_job "
		"WHERE grid = '%s' "
		"GROUP BY gridid "
		"HAVING total = matching "
		"LIMIT 100", 
		statToStr(stat), grid.c_str());

	DBResult res(this);
	res.use();
	while (res.fetch())
		ids.push_back(res.get_field(0));

}

void DBHandler::addDL(const string &jobid, const string &localName, const string &url)
{
	query("INSERT INTO cg_download (jobid, localname, url) VALUES '%s', '%s', '%s'",
		jobid.c_str(), localName.c_str(), url.c_str());
}

void DBHandler::deleteDL(const string &jobid, const string &localName)
{
	query("DELETE FROM cg_download WHERE jobid = '%s' AND localname = '%s'",
		jobid.c_str(), localName.c_str());
}

void DBHandler::updateDL(const string &jobid, const string &localName, const GTimeVal &next,
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

/**********************************************************************
 * Class: DBPool
 */

DBHandler *DBPool::get() throw (QMException *)
{
	DBHandler *dbh;

	/* Defer initialization until someone requests a new handle to ensure
	 * that global_config is initialized */
	if (!dbname)
		init();

	G_LOCK(dbhs);

	if (!free_dbhs.empty())
	{
		dbh = free_dbhs.front();
		free_dbhs.erase(free_dbhs.begin());
		goto out;
	}

	if (max_connections && used_dbhs.size() >= max_connections)
	{
		G_UNLOCK(dbhs);
		throw new QMException("Too many database connections are open");
	}

	dbh = new DBHandler(dbname, host, user, passwd);

out:
	used_dbhs.push_back(dbh);
	G_UNLOCK(dbhs);
	return dbh;
}

void DBPool::put(DBHandler *dbh)
{
	vector<DBHandler *>::iterator it;

	G_LOCK(dbhs);

	for (it = used_dbhs.begin(); it != used_dbhs.end(); it++)
		if (*it == dbh)
			break;
	if (it == used_dbhs.end())
		LOG(LOG_ERR, "Tried to put() a dbh that is not on the used list");
	else
		used_dbhs.erase(it);
	free_dbhs.push_back(dbh);
	G_UNLOCK(dbhs);
}

void DBPool::init()
{
	GError *error = NULL;

	g_static_mutex_init(&g__dbhs_lock);

	/* The database name is mandatory. Here we leak the GError but this is
	 * a non-recoverable error so... */
	dbname = g_key_file_get_string(global_config, GROUP_DATABASE, "name", &error);
	if (error)
		throw new QMException("Failed to retrieve the database name: %s", error->message);
	if (!dbname || !strlen(dbname))
		throw new QMException("The database name is not specified in the configuration file");

	/* These are not mandatory */
	host = g_key_file_get_string(global_config, GROUP_DATABASE, "host", NULL);
	user = g_key_file_get_string(global_config, GROUP_DATABASE, "user", NULL);
	passwd = g_key_file_get_string(global_config, GROUP_DATABASE, "password", NULL);

	/* max-connections is not mandatory, but if it is present it must be valid */
	max_connections = g_key_file_get_integer(global_config, GROUP_DATABASE, "max-connections", &error);
	if (error)
	{
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
			LOG(LOG_ERR, "Failed to parse the max DB connection number: %s", error->message);
		g_error_free(error);
	}
}

DBPool::~DBPool()
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
	g_static_mutex_free(&g__dbhs_lock);
}
