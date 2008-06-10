#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <map>
#include <string>
#include <cstring>

#include "Job.h"
#include "DBHandler.h"


using namespace std;


Job::Job(const char *id, const char *name, const char *grid, const char *args):
		id(id),name(name),args(args),grid(grid)
{
	algQ = AlgQueue::getInstance(grid, name);
}

void Job::addInput(const string &localname, const string &fsyspath)
{
	inputs[localname] = fsyspath;
}

void Job::addOutput(const string &localname, const string &fsyspath)
{
	outputs[localname] = fsyspath;
}

vector<string> Job::getInputs() const
{
	map<string, string>::const_iterator it;
	vector<string> rval;
	for (it = inputs.begin(); it != inputs.end(); it++)
		rval.push_back(it->first);
	return rval;
}

vector<string> Job::getOutputs() const
{
	map<string, string>::const_iterator it;
	vector<string> rval;
	for (it = outputs.begin(); it != outputs.end(); it++)
		rval.push_back(it->first);
	return rval;
}

void Job::setGridId(const string &sID)
{
	gridId = sID;

	DBHandler *dbH = DBHandler::get();
	dbH->updateJobGridID(id, gridId);
	DBHandler::put(dbH);
}


void Job::setStatus(JobStatus nStat)
{
	status = nStat;

	DBHandler *dbH = DBHandler::get();
	dbH->updateJobStat(id, status);
	DBHandler::put(dbH);
}

void Job::deleteJob()
{
	DBHandler *dbH = DBHandler::get();
	dbH->deleteJob(id);
	DBHandler::put(dbH);
}

void JobVector::clear()
{
	for (JobVector::iterator it = begin(); it != end(); it++)
		delete *it;
	vector<Job *>::clear();
}
