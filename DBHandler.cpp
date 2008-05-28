#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGJob.h"
#include "DBHandler.h"

#include <string>

#include <mysql.h>

#include "Logging.h"
#include "QMException.h"

using namespace std;

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
DBHandler::DBHandler(QMConfig &config)
{
	string dbname = config.getStr("DB_NAME");
	string host = config.getStr("DB_HOST");
	string user = config.getStr("DB_USER");
	string passwd = config.getStr("DB_PASSWORD");

	conn = mysql_init(0);
	if (!conn)
		throw QMException("Out of memory");
	if (!mysql_real_connect(conn, host.c_str(), user.c_str(), passwd.c_str(), dbname.c_str(), 0, 0, 0))
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

		algQ = CGAlgQueue::getInstance(algT, alg, this, 10);

		// Create new job descriptor
		CGJob *nJob = new CGJob(alg, args, algQ, this);
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
	if (query("SELECT * FROM cg_job WHERE status = '%s' ORDER BY creation_time"),
			getStatStr(stat))
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
string DBHandler::getAlgQStat(CGAlgType type, const string &name)
{
	string rv;
	DBResult res;

	if (!query("SELECT statistics FROM cg_algqueue WHERE dsttype='%s' AND alg='%s'", Alg2Str(type), name.c_str()))
		return rv;
	res.store(conn);
	rv = res.get_field(0);
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
