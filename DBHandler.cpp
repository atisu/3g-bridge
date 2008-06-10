#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGJob.h"
#include "DBHandler.h"

#include <string>

#include <stdlib.h>
#include <string.h>
#include <mysql.h>

#include "Logging.h"
#include "QMException.h"
#include "CGManager.h"

using namespace std;


static DBPool db_pool;

/* The order here must match the definition of CGJobStatus */
static const char *status_str[] =
{
	"INIT", "RUNNING", "FINISHED", "ERROR", "CANCEL"
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
		throw QMException("Invalid field index");
	return row[index];
}

const char *DBResult::get_field(const char *name)
{
	int i;

	for (i = 0; i < field_num && strcmp(fields[i].name, name); i++)
		/* Nothing */;
	if (i >= field_num)
		throw QMException("Unknown field requested: %s ", name);
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

/**
 * Constructor. Opens the connection to the database specified in the
 * parameters.
 *
 * @param[in] host Hostname of the MySQL DB
 * @param[in] user Username to connecto to the DB
 * @param[in] passwd Password to use
 * @param[in] dbname Name of the DB to use
 */
DBHandler::DBHandler(const char *dbname, const char *host, const char *user, const char *passwd)
{
	conn = mysql_init(0);
	if (!conn)
		throw QMException("Out of memory");
	if (!mysql_real_connect(conn, host, user, passwd, dbname, 0, 0, 0))
		throw QMException("Could not connect to the database: %s", mysql_error(conn));
}

/**
 * Destructor. Close connection to the DB.
 */
DBHandler::~DBHandler()
{
	mysql_close(conn);
}


/**
 * Convert CGJobStatus to string.
 *
 * @param[in] stat Status info
 * @return String representation of the requested status
 */
const char *DBHandler::getStatStr(CGJobStatus stat)
{
	if (stat < 0 || stat > (int)(sizeof(status_str) / sizeof(status_str[0])))
		throw QMException("Unknown job status value %d", (int)stat);
	return status_str[stat];
}


/**
 * Parse job results of a query.
 *
 * @param[in] squery Pointer to the query to perform on the cg_job table
 * @return Pointer to a newly allocated vector of pointer to CGJobs. Should be
 *         freed by the caller
 */
void DBHandler::parseJobs(JobVector &jobs)
{
	DBResult res(this);

	res.use();
	while (res.fetch())
	{
		// Get instance of the relevant algorithm queue
		CGAlgQueue *algQ;

		const char *alg = res.get_field("alg");
		const char *grid = res.get_field("grid");
		const char *args = res.get_field("args");
		const char *gridid = res.get_field("gridid");
		const char *id = res.get_field("id");

		algQ = CGAlgQueue::getInstance(grid, alg);

		// Create new job descriptor
		CGJob *nJob = new CGJob(id, alg, args, algQ);
		if (gridid)
			nJob->setGridId(gridid);

		// Get inputs for job from db
		DBHandler *dbh = get();
		if (!dbh->query("SELECT localname, path FROM cg_inputs WHERE id = '%s'", id))
		{
			delete nJob;
			put(dbh);
			continue;
		}

		DBResult res2(dbh);
		res2.use();
		while (res2.fetch())
			nJob->addInput(res2.get_field(0), res2.get_field(1));

		// Get outputs for job from db
		if (!dbh->query("SELECT localname, path FROM cg_outputs WHERE id = '%s'", id))
		{
			delete nJob;
			put(dbh);
			continue;
		}

		res2.use();
		while (res2.fetch())
			nJob->addOutput(res2.get_field(0), res2.get_field(1));
		put(dbh);

		jobs.push_back(nJob);
	}
}


void DBHandler::getJobs(JobVector &jobs, const string &grid, const string &alg, CGJobStatus stat, int batch)
{
	if (query("SELECT * FROM cg_job "
			"WHERE grid = '%s' AND alg = '%s' AND status = '%s' "
			"ORDER BY creation_time",
			grid.c_str(), alg.c_str(), getStatStr(stat)))
		return parseJobs(jobs);
}

void DBHandler::getJobs(JobVector &jobs, const string &grid, CGJobStatus stat, int batch)
{
	if (query("SELECT * FROM cg_job "
			"WHERE grid = '%s' AND status = '%s' "
			"ORDER BY creation_time",
			grid.c_str(), getStatStr(stat)))
		return parseJobs(jobs);
}


/**
 * Get jobs with a given grid identifier.
 *
 * @param[in] stat Identifier we're interested in
 * @return Pointer to a newly allocated vector of pointer to CGJobs having the
 *         requested grid identier. Should be freed by the caller
 */
void DBHandler::getJobs(JobVector &jobs, const char *gridID)
{
	if (query("SELECT * FROM cg_job WHERE gridid = '%s'", gridID))
		return parseJobs(jobs);
}


/**
 * Get algorithm queue statistics from the database.
 */
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
		new CGAlgQueue(res.get_field(0), res.get_field(1), size, statStr);
	}
}


/**
 * Update algorithm queue statistics and store the new statistics in the
 * database.
 *
 * @param[in] algQ The algorithm queue
 * @param[in] pSize Size of the package processed (i.e. number of jobs in a
 *                  package)
 * @param[in] pTime Time used to process the package
 */
void DBHandler::updateAlgQStat(CGAlgQueue *algQ, unsigned pSize, unsigned pTime)
{
	algQ->updateStat(pSize, pTime);

	query("UPDATE cg_algqueue SET statistics='%s' WHERE dsttype='%s' AND alg='%s'",
		algQ->getStatStr().c_str(), algQ->getGrid().c_str(), algQ->getName().c_str());
}


/**
 * Update algorithm queue statistics and store the new statistics in the
 * database.
 *
 * @param[in] gridId The finished grid identifier
 * @param[in] pSize Size of the package processed (i.e. number of jobs in a
 *                  package)
 * @param[in] pTime Time used to process the package
 */
void DBHandler::updateAlgQStat(const char *gridId, unsigned pSize, unsigned pTime)
{
	JobVector jobs;
	getJobs(jobs, gridId);
	CGAlgQueue *algQ = jobs.at(0)->getAlgQueue();
	updateAlgQStat(algQ, pSize, pTime);
}


/**
 * Update grid identifier of a job.
 *
 * @param[in] ID The job's identifier
 * @param[in] gridID The grid identifier to set
 */
void DBHandler::updateJobGridID(const string &ID, const string &gridID)
{
	query("UPDATE cg_job SET gridid='%s' WHERE id='%s'", gridID.c_str(), ID.c_str());
}


/**
 * Update status of a job.
 *
 * @param[in] ID The job's identifier
 * @param[in] newstat The status to set
 */
void DBHandler::updateJobStat(const string &ID, CGJobStatus newstat)
{
	query("UPDATE cg_job SET status='%s' WHERE id='%s'", getStatStr(newstat), ID.c_str());
}


void DBHandler::deleteJob(const string &ID)
{
	query("DELETE FROM cg_job WHERE id='%s'", ID.c_str());
	query("DELETE FROM cg_inputs WHERE id='%s'", ID.c_str());
	query("DELETE FROM cg_outputs WHERE id='%s'", ID.c_str());
}


void DBHandler::addJob(CGJob &job)
{
	bool success = true;
	query("START TRANSACTION");

	success &= query("INSERT INTO cg_job (id, alg, status, args) VALUES ('%s', '%s', 'INIT', '%s')",
		job.getId().c_str(), job.getName().c_str(), job.getArgs().c_str());

	vector<string> inputs = job.getInputs();
	for (vector<string>::const_iterator it = inputs.begin(); it != inputs.end(); it++)
	{
		string path = job.getInputPath(*it);
		success &= query("INSERT INTO cg_inputs (id, localname, path) VALUES ('%s', '%s', '%s')",
			job.getId().c_str(), it->c_str(), path.c_str());
	}
	vector<string> outputs = job.getOutputs();
	for (vector<string>::const_iterator it = outputs.begin(); it != outputs.end(); it++)
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

DBHandler *DBHandler::get() throw (QMException &)
{
	return db_pool.get();
}

void DBHandler::put(DBHandler *dbh)
{
	db_pool.put(dbh);
}

/**
 * Add an algorithm queue to the database. Initially, the statistics for the
 * algorithm are set empty.
 *
 * @param[in] grid The grid's name
 * @param[in] alg The algorithm's name
 * @param[in] batchsize Maximum batch size for the algorithm
 */
void DBHandler::addAlgQ(const char *grid, const char *alg, unsigned int batchsize)
{
	query("INSERT INTO cg_algqueue(grid, alg, batchsize, statistics) VALUES('%s', '%s', '%u', '')",
		grid, alg, batchsize);
}

/**********************************************************************
 * Class: DBPool
 */

DBHandler *DBPool::get() throw (QMException &)
{
	DBHandler *dbh;

	if (!free_dbhs.empty())
	{
		dbh = free_dbhs.front();
		free_dbhs.erase(free_dbhs.begin());
		used_dbhs.push_back(dbh);
		return dbh;
	}

	/* Defer initialization until someone requests a new handle to ensure
	 * that global_config is initialized */
	if (!dbname)
		init();

	if (max_connections && used_dbhs.size() >= max_connections)
		throw QMException("Too many database connections are open");

	dbh = new DBHandler(dbname, host, user, passwd);

	used_dbhs.push_back(dbh);
	return dbh;
}

void DBPool::put(DBHandler *dbh)
{
	vector<DBHandler *>::iterator it;
	for (it = used_dbhs.begin(); it != used_dbhs.end(); it++)
		if (*it == dbh)
			break;
	if (it == used_dbhs.end())
		LOG(LOG_ERR, "Tried to put() a dbh that is not on the used list");
	else
		used_dbhs.erase(it);
	free_dbhs.push_back(dbh);
}

void DBPool::init()
{
	GError *error = NULL;

	/* The database name is mandatory. Here we leak the GError but this is
	 * a non-recoverable error so... */
	dbname = g_key_file_get_string(global_config, "database", "name", &error);
	if (error)
		throw QMException("Failed to retrieve the database name: %s", error->message);

	/* These are not mandatory */
	host = g_key_file_get_string(global_config, "database", "host", NULL);
	user = g_key_file_get_string(global_config, "database", "user", NULL);
	passwd = g_key_file_get_string(global_config, "database", "password", NULL);

	/* max-connections is not mandatory, but if it is present it must be valid */
	max_connections = g_key_file_get_integer(global_config, "database", "max-connections", &error);
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
}
