#include "CGJob.h"
#include "JobDB.h"
#include "CGSqlStruct.h"

#include <string>
#include <mysql++/mysql++.h>

using namespace std;


JobDB::JobDB(const string host, const string user, const string passwd, const string dbname):thost(host),tuser(user),tpasswd(passwd),tdbname(dbname)
{
  conn = new Connection(dbname.c_str(), host.c_str(), user.c_str(), passwd.c_str());
}


JobDB::~JobDB()
{
  conn->disconnect();
  delete conn;
}


vector<CGJob *> *JobDB::getJobs(CGJobStatus stat)
{
  string statStr;
  Query query = conn->query();
  
  switch (stat) {
  case CG_INIT:
    statStr = "CG_INIT";
    break;
  case CG_RUNNING:
    statStr = "CG_RUNNING";
    break;
  case CG_FINISHED:
    statStr = "CG_FINISHED";
    break;
  case CG_ERROR:
    statStr = "CG_ERROR";
    break;
  default:
    return new vector<CGJob *>();
    break;
  }
  query << "SELECT * FROM cg_job WHERE status = \"" << statStr << "\"";
  return parseJobs(&query);
}


vector<CGJob *> *JobDB::parseJobs(Query *squery)
{
  vector<cg_job> source;
  squery->storein(source);
  vector<CGJob *> *jobs = new vector<CGJob *>();
  for (vector<cg_job>::iterator it = source.begin(); it != source.end(); it++) {
    string id, name, cmdlineargs, token, algname, wuid;
    Query query = conn->query();
    
    id = it->id;
    name = it->alg;
    cmdlineargs = it->args;
    algname = it->alg;
    wuid = it->wuid;
    
    // Vectorize cmdlineargs string
    list<string> *arglist = new list<string>();
    istringstream iss(cmdlineargs);
    while (getline(iss, token, ' '))
      arglist->push_back(token);
    
    // Find out which algorithm the job belongs to
    map<string, CGAlgQueue *>::iterator at = talgs->find(algname);
    //!!!! What happens if...?
    if (at == talgs->end())
      return jobs;
    CGAlg *alg = at->second->getType();
    
    // Create new job descriptor
    CGJob *nJob = new CGJob(name, arglist, *alg);
    
    // Get inputs for job from db
    query.reset();
    query << "SELECT * FROM cg_inputs WHERE jobid = " << id;
    vector<cg_inputs> in;
    query.storein(in);
    for (vector<cg_inputs>::iterator it = in.begin(); it != in.end(); it++)
      nJob->addInput(it->localname, it->path);
    
    // Get outputs for job from db
    query.reset();
    query << "SELECT * FROM cg_outputs WHERE jobid = " << id;
    vector<cg_outputs> out;
    query.storein(out);
    for (vector<cg_outputs>::iterator it = out.begin(); it != out.end(); it++)
      nJob->addOutput(string(it->localname));
    
    // Also register stdout and stderr
    nJob->addOutput("stdout.txt");
    nJob->addOutput("stderr.txt");
    
    jobs->push_back(nJob);
  }
  return jobs;
}


vector<CGJob *> *JobDB::getJobs(string gridID)
{
  Query query = conn->query();
  query << "SELECT * FROM cg_inputs WHERE wuid = " << gridID;
  return parseJobs(&query);
}


void JobDB::addJobs(vector<CGJob *> *jobs)
{
}


void JobDB::updateJobStat(string gridID, CGJobStatus newstat)
{
}
