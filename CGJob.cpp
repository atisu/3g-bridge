#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <map>
#include <string>
#include <cstring>

#include "CGJob.h"
#include "DBHandler.h"


using namespace std;


CGJob::CGJob(const string tname, string args, CGAlgQueue *algQ):name(tname),talgQ(algQ),targs(args)
{
    inputs.clear();
    outputs.clear();
}

CGJob::~CGJob()
{
}

void CGJob::addInput(const string localname, const string fsyspath)
{
    inputs[localname] = fsyspath;
}

void CGJob::addOutput(const string localname, const string fsyspath)
{
    outputs[localname] = fsyspath;
}

vector<string> CGJob::getInputs() const
{
    map<string, string>::const_iterator it;
    vector<string> rval;
    for (it = inputs.begin(); it != inputs.end(); it++) {
	rval.push_back(it->first);
    }
    return rval;
}

vector<string> CGJob::getOutputs() const
{
    map<string, string>::const_iterator it;
    vector<string> rval;
    for (it = outputs.begin(); it != outputs.end(); it++) {
	rval.push_back(it->first);
    }
    return rval;
}

string CGJob::getInputPath(const string localname)
{
    return inputs[localname];
}

string CGJob::getOutputPath(const string localname)
{
    return outputs[localname];
}


void CGJob::setGridId(const string &sID)
{
	gridId = sID;

	DBHandler *dbH = DBHandler::get();
	dbH->updateJobGridID(id, gridId);
	DBHandler::put(dbH);
}


void CGJob::setStatus(CGJobStatus nStat)
{
	status = nStat;
	DBHandler *dbH = DBHandler::get();
	dbH->updateJobStat(id, status);
	DBHandler::put(dbH);
}


void CGJob::deleteJob()
{
	DBHandler *dbH = DBHandler::get();
	dbH->deleteJob(id);
	DBHandler::put(dbH);
}
