#include "CGJob.h"
#include "DBHandler.h"
#include "CGSqlStruct.h"

#include <string>
#include <mysql++/mysql++.h>

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
DBHandler::DBHandler(const string host, const string user, const string passwd, const string dbname):thost(host),tuser(user),tpasswd(passwd),tdbname(dbname)
{
	conn = new Connection(dbname.c_str(), host.c_str(), user.c_str(), passwd.c_str());
}

/**
 * Destructor. Close connection to the DB.
 */
DBHandler::~DBHandler()
{
//	conn->disconnect();
	delete conn;
}


/**
 * Convert CGJobStatus to string.
 *
 * @param[in] stat Status info
 * @return String representation of the requested status
 */
string DBHandler::getStatStr(CGJobStatus stat)
{
	string statStr;

	switch (stat) {
	case INIT:
		statStr = "INIT";
		break;
	case RUNNING:
		statStr = "RUNNING";
		break;
	case FINISHED:
		statStr = "FINISHED";
		break;
	case ERROR:
		statStr = "ERROR";
		break;
	default:
		statStr = "";
		break;
	}

	return statStr;
}


/**
 * Convert algorithm to string.
 *
 * @param[in] type Algorithm type to convert
 * @return String representation of the algorithm type
 */
string DBHandler::Alg2Str(CGAlgType type)
{
	string tStr = "UNKNOWN";

	switch (type) {
	case CG_ALG_DB:
		tStr = "DB";
		break;
	case CG_ALG_LOCAL:
		tStr = "LOCAL";
		break;
	case CG_ALG_DCAPI:
		tStr = "DCAPI";
		break;
	case CG_ALG_EGEE:
		tStr = "EGEE";
		break;
	default:
		tStr = "UNKNOWN";
		break;
	}
	
	return tStr;
}


/**
 * Convert string to algorithm.
 *
 * @param[in] type Algorithm string to convert
 * @return Algorithm type representation of the string
 */
CGAlgType DBHandler::Str2Alg(const string name)
{
	if (name == "DB")
		return CG_ALG_DB;

	if (name == "LOCAL")
		return CG_ALG_LOCAL;

	if (name == "DCAPI")
		return CG_ALG_DCAPI;

	if (name == "EGEE")
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

	query << "SELECT * FROM cg_job WHERE status = \"" << getStatStr(stat) << "\"";

	return parseJobs(&query);
}


/**
 * Parse job results of a query.
 *
 * @param[in] squery Pointer to the query to perform on the cg_job table
 * @return Pointer to a newly allocated vector of pointer to CGJobs having the
 *         requested status. Should be freed by the caller
 */
vector<CGJob *> *DBHandler::parseJobs(Query *squery)
{
	vector<cg_job> source;
	squery->storein(source);
	vector<CGJob *> *jobs = new vector<CGJob *>();

	for (vector<cg_job>::iterator it = source.begin(); it != source.end(); it++) {
		string name, token, algname;
		Query query = conn->query();

		name = it->alg;

		// Get instance of the relevant algorithm queue
		CGAlgQueue *algQ;
		CGAlgType algT = Str2Alg(it->dsttype);
		algQ = CGAlgQueue::getInstance(algT, name, this, 10);

		// Create new job descriptor
		CGJob *nJob = new CGJob(it->alg, it->args, algQ);
		nJob->setId(it->id);
		nJob->setGridId(it->gridid);
		nJob->setDstType(Str2Alg(it->dsttype));
		nJob->setDstLoc(it->dstloc);

		// Get inputs for job from db
		query.reset();
		query << "SELECT * FROM cg_inputs WHERE id = \"" << it->id << "\"";
		vector<cg_inputs> in;
		query.storein(in);
		for (vector<cg_inputs>::iterator in_it = in.begin(); in_it != in.end(); in_it++)
			nJob->addInput(in_it->localname, in_it->path);

		// Get outputs for job from db
		query.reset();
		query << "SELECT * FROM cg_outputs WHERE id = \"" << it->id << "\"";
		vector<cg_outputs> out;
		query.storein(out);
		for (vector<cg_outputs>::iterator out_it = out.begin(); out_it != out.end(); out_it++) {
    			nJob->addOutput(out_it->localname);
			if (string(out_it->path) != "")
				nJob->setOutputPath(out_it->localname, out_it->path);
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
	vector<cg_algqueue> algQs;
	query.storein(algQs);

	if (algQs.size())
		rv = algQs[0].statistics;

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


/**
 * Update output file path information of a job's output file.
 *
 * @param[in] ID The job's identifier
 * @param[in] localname The local name of the file
 * @param[in] pathname The path to set for the given job's file
 */
void DBHandler::updateOutputPath(string ID, string localname, string pathname)
{
	Query query = conn->query();
	query << "UPDATE cg_outputs SET path=\"" << pathname << "\" WHERE id=\"" << ID << "\" AND localname=\"" << localname << "\"";
	query.execute();
}
