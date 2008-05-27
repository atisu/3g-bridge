#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGJob.h"
#include "DBHandler.h"

#include <string>
#include <mysql++/mysql++.h>

#include "Logging.h"

using namespace std;


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
	dbname = config.getStr("DB_NAME");
	host = config.getStr("DB_HOST");
	user = config.getStr("DB_USER");
	passwd = config.getStr("DB_PASSWORD");
	try {
	    conn = new Connection(use_exceptions);
	    conn->connect(dbname.c_str(), host.c_str(), user.c_str(), passwd.c_str());
	} catch (ConnectionFailed& ex) {
	    LOG(LOG_ERR, "%s", conn->error());
	    exit(-1);
	}
	if (conn->connected()) {
	    LOG(LOG_INFO, "Database connection established successfully.");
	}
}

/**
 * Destructor. Close connection to the DB.
 */
DBHandler::~DBHandler()
{
	conn->close();
	delete conn;
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
		throw string("Unknown job status value");
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
 * Get jobs with a given status.
 *
 * @param[in] stat Status we're interested in
 * @return Pointer to a newly allocated vector of pointer to CGJobs having the
 *         requested status. Should be freed by the caller
 */
vector<CGJob *> *DBHandler::getJobs(CGJobStatus stat)
{
	Query query = conn->query();

	query << "SELECT * FROM cg_job WHERE status = \"" << getStatStr(stat) << "\" ORDER BY creation_time";
	
	return parseJobs(&query);
}


/**
 * Parse job results of a query.
 *
 * @param[in] squery Pointer to the query to perform on the cg_job table
 * @return Pointer to a newly allocated vector of pointer to CGJobs. Should be
 *         freed by the caller
 */
vector<CGJob *> *DBHandler::parseJobs(Query *squery)
{
	Result rRes = squery->store();
	vector<CGJob *> *jobs = new vector<CGJob *>();

	for (size_t i = 0; i < rRes.num_rows(); i++) {
		// Get instance of the relevant algorithm queue
		CGAlgQueue *algQ;
#ifdef HAVE_DCAPI
		CGAlgType algT = Str2Alg("DCAPI");
#endif
#ifdef HAVE_EGEE
		CGAlgType algT = Str2Alg("EGEE");
#endif
		Row tRow = rRes.at(i);
		algQ = CGAlgQueue::getInstance(algT, string(tRow["alg"]), this, 10);

		// Create new job descriptor
		string args = (tRow["args"].is_null() ? "" : string(tRow["args"]));
		CGJob *nJob = new CGJob(string(tRow["alg"]), args, algQ, this);
		nJob->setId(string(tRow["id"]));
		nJob->setGridId(tRow["gridid"].is_null() ? "" : string(tRow["gridid"]));

		// Get inputs for job from db
		Query query = conn->query();
		query.reset();
		query << "SELECT * FROM cg_inputs WHERE id = " << tRow["id"];
		Result iRes = query.store();
		for (size_t ii = 0; ii != iRes.num_rows(); ii++)
			nJob->addInput(string(iRes.at(ii)["localname"]), string(iRes.at(ii)["path"]));

		// Get outputs for job from db
		query.reset();
		query << "SELECT * FROM cg_outputs WHERE id = " << tRow["id"] << "";
		Result oRes = query.store();
		for (size_t oi = 0; oi != oRes.num_rows(); oi++) {
    			nJob->addOutput(string(oRes.at(oi)["localname"]));
			if (!oRes.at(oi)["path"].is_null())
				nJob->setOutputPath(string(oRes.at(oi)["localname"]), string(oRes.at(oi)["path"]));
		}

		jobs->push_back(nJob);
	}

	return jobs;
}


/**
 * Get jobs with a given grid identifier.
 *
 * @param[in] stat Identifier we're interested in
 * @return Pointer to a newly allocated vector of pointer to CGJobs having the
 *         requested grid identier. Should be freed by the caller
 */
vector<CGJob *> *DBHandler::getJobs(string gridID)
{
	Query query = conn->query();

	query << "SELECT * FROM cg_job WHERE gridid = \"" << gridID << "\"";

	return parseJobs(&query);
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
	string rv = "";
	Query query = conn->query();

	query << "SELECT * FROM cg_algqueue WHERE dsttype=\"" << Alg2Str(type) << "\" AND alg=\"" << name << "\"";
	vector<Row> algQs;
	query.storein(algQs);

	if (algQs.size())
		rv = string(algQs[0]["statistics"]);

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

	Query query = conn->query();
	query << "UPDATE cg_algqueue SET statistics=\"" << algQ->getStatStr() << "\" WHERE ";
	query << "dsttype=\"" << Alg2Str(algQ->getType()) << "\" AND alg=\"" << algQ->getName() << "\"";
	query.execute();
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
	vector<CGJob *> *jobs = getJobs(string(gridId));
	CGAlgQueue *algQ = jobs->at(0)->getAlgQueue();
	algQ->updateStat(pSize, pTime);

	Query query = conn->query();
	query << "UPDATE cg_algqueue SET statistics=\"" << algQ->getStatStr() << "\" WHERE ";
	query << "dsttype=\"" << Alg2Str(algQ->getType()) << "\" AND alg=\"" << algQ->getName() << "\"";
	query.execute();

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
	Query query = conn->query();
	query << "UPDATE cg_job SET gridid=\"" << gridID << "\" WHERE id=\"" << ID << "\"";
	query.execute();
}


/**
 * Update status of a job.
 *
 * @param[in] ID The job's identifier
 * @param[in] newstat The status to set
 */
void DBHandler::updateJobStat(string ID, CGJobStatus newstat)
{
	Query query = conn->query();
	query << "UPDATE cg_job SET status=\"" << getStatStr(newstat) << "\" WHERE id=\"" << ID << "\"";
	query.execute();
}


void DBHandler::deleteJob(const string &ID)
{
	Query query = conn->query();
	query << "DELETE FROM cg_job WHERE id=\"" << ID << "\"";
	query.execute();

	query.reset();
	query << "DELETE FROM cg_inputs WHERE id=\"" << ID << "\"";
	query.execute();

	query.reset();
	query << "DELETE FROM cg_outputs WHERE id=\"" << ID << "\"";
	query.execute();
}
