#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGJob.h"
#include "DBHandler.h"

#include <string>

#include <mysql.h>

#include "Logging.h"
#include "QMException.h"
#include "CGManager.h"

using namespace std;


static DBPool db_pool;

DBResult::~DBResult()
{
	if (res)
		mysql_free_result(res);
}

void DBResult::store(MYSQL *dbh)
{
	/* Allow re-use */
	if (res)
		mysql_free_result(res);

	field_num = mysql_field_count(dbh);
	res = mysql_store_result(dbh);
	if (field_num && !res)
		throw QMException("Failed to fetch results: %s", mysql_error(dbh));
	fields = mysql_fetch_fields(res);
}

bool DBResult::fetch()
{
	row = mysql_fetch_row(res);
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
	if (mysql_ping(conn))
		throw QMException("The connection to the database is broken: %s", mysql_error(conn));
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
	switch (stat) {
	case INIT:
		return "INIT";
	case RUNNING:
		return "RUNNING";
	case FINISHED:
		return "FINISHED";
	case ERROR:
		return "ERROR";
	case CANCEL:
		return "CANCEL";
	default:
		throw QMException("Unknown job status value %d", (int)stat);
	}
}


/**
 * Convert algorithm to string.
 *
 * @param[in] type Algorithm type to convert
 * @return String representation of the algorithm type
 */
const char *DBHandler::Alg2Str(CGAlgType type)
{
	switch (type) {
	case CG_ALG_DB:
		return "DB";
	case CG_ALG_LOCAL:
		return "LOCAL";
	case CG_ALG_DCAPI:
		return "DCAPI";
	case CG_ALG_EGEE:
		return "EGEE";
	default:
		return "UNKNOWN";
	}
}


/**
 * Convert string to algorithm.
 *
 * @param[in] type Algorithm string to convert
 * @return Algorithm type representation of the string
 */
CGAlgType DBHandler::Str2Alg(const char *name)
{
	if (!strcmp(name, "DB"))
		return CG_ALG_DB;

	if (!strcmp(name, "LOCAL"))
		return CG_ALG_LOCAL;

	if (!strcmp(name, "DCAPI"))
		return CG_ALG_DCAPI;

	if (!strcmp(name, "EGEE"))
		return CG_ALG_EGEE;

	return CG_ALG_EGEE;
}

/**
 * Parse job results of a query.
 *
 * @param[in] squery Pointer to the query to perform on the cg_job table
 * @return Pointer to a newly allocated vector of pointer to CGJobs. Should be
 *         freed by the caller
 */
vector<CGJob *> *DBHandler::parseJobs(void)
{
	DBResult res;
	vector<CGJob *> *jobs = new vector<CGJob *>();

	try
	{
		res.store(conn);
	}
	catch (QMException &e)
	{
		LOG(LOG_ERR, "%s", e.what());
		return jobs;
	}

	while (res.fetch())
	{
		// Get instance of the relevant algorithm queue
		CGAlgQueue *algQ;
#ifdef HAVE_DCAPI
		CGAlgType algT = Str2Alg("DCAPI");
#endif
#ifdef HAVE_EGEE
		CGAlgType algT = Str2Alg("EGEE");
#endif
		const char *alg = res.get_field("alg");
		const char *args = res.get_field("args");
		const char *gridid = res.get_field("gridid");
		const char *id = res.get_field("id");

		algQ = CGAlgQueue::getInstance(algT, alg, 10);

		// Create new job descriptor
		CGJob *nJob = new CGJob(alg, args, algQ);
		nJob->setId(id);
		if (gridid)
			nJob->setGridId(gridid);

		// Get inputs for job from db
		query("SELECT localname, path FROM cg_inputs WHERE id = '%s'", id);
		DBResult res2;
		res2.store(conn);
		while (res2.fetch())
			nJob->addInput(res2.get_field(0), res2.get_field(1));

		// Get outputs for job from db
		query("SELECT localname, path FROM cg_outputs WHERE id = '%s'", id);
		res2.store(conn);
		while (res2.fetch())
			nJob->addOutput(res2.get_field(0), res2.get_field(1));

		jobs->push_back(nJob);
	}

	return jobs;
}


/**
 * Get jobs with a given status.
 *
 * @param[in] stat Status we're interested in
 * @return Pointer to a newly allocated vector of pointer to CGJobs having the
 *         requested status. Should be freed by the caller
 */
vector<CGJob *> *DBHandler::getJobs(CGJobStatus stat)
{
	if (query("SELECT * FROM cg_job WHERE status = '%s' ORDER BY creation_time",
			getStatStr(stat)))
		return parseJobs();
	return 0;
}



/**
 * Get jobs with a given grid identifier.
 *
 * @param[in] stat Identifier we're interested in
 * @return Pointer to a newly allocated vector of pointer to CGJobs having the
 *         requested grid identier. Should be freed by the caller
 */
vector<CGJob *> *DBHandler::getJobs(const char *gridID)
{
	if (query("SELECT * FROM cg_job WHERE gridid = '%s'", gridID))
		return parseJobs();
	return 0;
}


/**
 * Get algorithm queue statistics from the database.
 *
 * @param[in] type Algorithm type
 * @param[in] name Algorithm name
 * @return statistics field for the given algorithm queue in the DB, and ""
 *         if no such entry exists
 */
string DBHandler::getAlgQStat(CGAlgType type, const string &name, unsigned *ssize)
{
	string rv = "";
	*ssize = 1;
	DBResult res;

	if (!query("SELECT batchsize,statistics FROM cg_algqueue WHERE grid='%s' AND alg='%s'", Alg2Str(type), name.c_str()))
		return rv;
	res.store(conn);
	if (res.fetch()) {
		*ssize = atoi(res.get_field(0));
		rv = res.get_field(1);
	}
	return rv;
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
		algQ->getStatStr().c_str(), Alg2Str(algQ->getType()), algQ->getName().c_str());
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
	vector<CGJob *> *jobs = getJobs(gridId);
	if (!jobs)
		return;
	CGAlgQueue *algQ = jobs->at(0)->getAlgQueue();
	updateAlgQStat(algQ, pSize, pTime);
	for (unsigned i = 0; i < jobs->size(); i++)
		delete jobs->at(i);
	delete jobs;
}


/**
 * Update grid identifier of a job.
 *
 * @param[in] ID The job's identifier
 * @param[in] gridID The grid identifier to set
 */
void DBHandler::updateJobGridID(string ID, string gridID)
{
	query("UPDATE cg_job SET gridid='%s' WHERE id='%s'", gridID.c_str(), ID.c_str());
}


/**
 * Update status of a job.
 *
 * @param[in] ID The job's identifier
 * @param[in] newstat The status to set
 */
void DBHandler::updateJobStat(string ID, CGJobStatus newstat)
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
	query("START TRANSACTION");
	try
	{
		query("INSERT INTO cg_job (id, alg, status, args) VALUES ('%s', '%s', 'INIT', '%s')",
			job.getId().c_str(), job.getName().c_str(), job.getArgs().c_str());
		vector<string> inputs = job.getInputs();
		for (vector<string>::const_iterator it = inputs.begin(); it != inputs.end(); it++)
		{
			string path = job.getInputPath(*it);
			query("INSERT INTO cg_inputs (id, localname, path) VALUES ('%s', '%s', '%s')",
				job.getId().c_str(), it->c_str(), path.c_str());
		}
		vector<string> outputs = job.getOutputs();
		for (vector<string>::const_iterator it = outputs.begin(); it != outputs.end(); it++)
		{
			string path = job.getOutputPath(*it);
			query("INSERT INTO cg_outputs (id, localname, path) VALUES ('%s', '%s', '%s')",
				job.getId().c_str(), it->c_str(), path.c_str());
		}
		query("COMMIT");
	}
	catch (QMException &e)
	{
		query("ROLLBACK");
		throw;
	}
}

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

	if (max_connections && used_dbhs.size() >= max_connections)
		throw QMException("Too many database connections are open");

	dbh = new DBHandler(dbname.c_str(), host.c_str(), user.c_str(), passwd.c_str());

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

DBPool::DBPool()
{
	GError *error = NULL;

	/* Store the connection parameters locally for easier memory management */
	char *str = g_key_file_get_string(global_config, "database", "name", NULL);
	dbname = str ? str : "";
	g_free(str);
	str = g_key_file_get_string(global_config, "database", "host", NULL);
	host = str ? str : "";
	g_free(str);
	str = g_key_file_get_string(global_config, "database", "user", NULL);
	user = str ? str : "";
	g_free(str);
	str = g_key_file_get_string(global_config, "database", "password", NULL);
	passwd = str ? str : "";
	g_free(str);

	max_connections = g_key_file_get_int(global_config, "database", "max-connections", &error);
	if (error)
	{
		if (error->code != G_KEY_FILE_KEY_NOT_FOUND)
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
}

DBHandler *DBHandler::get()
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
