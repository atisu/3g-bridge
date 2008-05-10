#include "CGJob.h"
#include "DBHandler.h"
#include "CGSqlStruct.h"

#include <string>
#include <mysql++/mysql++.h>

using namespace std;


DBHandler::DBHandler(const string host, const string user, const string passwd, const string dbname):thost(host),tuser(user),tpasswd(passwd),tdbname(dbname)
{
  conn = new Connection(dbname.c_str(), host.c_str(), user.c_str(), passwd.c_str());
}


DBHandler::~DBHandler()
{
  conn->disconnect();
  delete conn;
}


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


vector<CGJob *> *DBHandler::getJobs(CGJobStatus stat)
{
  Query query = conn->query();
  string statStr = getStatStr(stat);
  if (statStr == "")
    return new vector<CGJob *>();
  
  query << "SELECT * FROM cg_job WHERE status = \"" << statStr << "\"";
  return parseJobs(&query);
}


vector<CGJob *> *DBHandler::parseJobs(Query *squery)
{
  vector<cg_job> source;
  squery->storein(source);
  vector<CGJob *> *jobs = new vector<CGJob *>();
  cout << source.size() << endl;
  for (vector<cg_job>::iterator it = source.begin(); it != source.end(); it++) {
    string id, name, cmdlineargs, token, algname, gridid;
    Query query = conn->query();
    
    id = it->id;
    name = it->alg;
    cmdlineargs = it->args;
    algname = it->alg;
    gridid = it->gridid;
    
    // Get instance of the job's algorithm queue
#ifdef HAVE_DCAPI
    CGAlgQueue *algQ = CGAlgQueue::getInstance(CG_ALG_DCAPI, algname);
#endif
#ifdef HAVE_EGEE
    CGAlgQueue *algQ = CGAlgQueue::getInstance(CG_ALG_EGEE, algname);
#endif
    
    // Create new job descriptor
    CGJob *nJob = new CGJob(name, cmdlineargs, algQ);
    nJob->setId(id);
    nJob->setGridId(gridid);
    nJob->setDstType(algQ->getType());
    nJob->setDstLoc(it->dstloc);
    
    // Get inputs for job from db
    query.reset();
    query << "SELECT * FROM cg_inputs WHERE id = \"" << id << "\"";
    vector<cg_inputs> in;
    query.storein(in);
    for (vector<cg_inputs>::iterator it = in.begin(); it != in.end(); it++)
      nJob->addInput(it->localname, it->path);
    
    // Get outputs for job from db
    query.reset();
    query << "SELECT * FROM cg_outputs WHERE id = \"" << id << "\"";
    vector<cg_outputs> out;
    query.storein(out);
    for (vector<cg_outputs>::iterator it = out.begin(); it != out.end(); it++) {
      nJob->addOutput(string(it->localname));
      if (string(it->path) != "")
        nJob->setOutputPath(string(it->localname), string(it->path));
    }
    
    // Also register stdout and stderr
    //nJob->addOutput("stdout.txt");
    //nJob->addOutput("stderr.txt");
    
    jobs->push_back(nJob);
  }
  return jobs;
}


vector<CGJob *> *DBHandler::getJobs(string gridID)
{
  Query query = conn->query();
  query << "SELECT * FROM cg_inputs WHERE gridid = \"" << gridID << "\"";
  return parseJobs(&query);
}


void DBHandler::addJobs(vector<CGJob *> *jobs)
{
}


void DBHandler::updateJobGridID(string ID, string gridID)
{
  Query query = conn->query();
  query << "UPDATE cg_job SET gridid=\"" << gridID << "\" WHERE id=\"" << ID << "\"";
  query.execute();
}


void DBHandler::updateJobStat(string ID, CGJobStatus newstat)
{
  Query query = conn->query();
  query << "UPDATE cg_job SET status=\"" << getStatStr(newstat) << "\" WHERE id=\"" << ID << "\"";
  query.execute();
}


void DBHandler::updateOutputPath(string ID, string localname, string pathname)
{
  Query query = conn->query();
  query << "UPDATE cg_outputs SET path=\"" << pathname << "\" WHERE id=\"" << ID << "\" AND localname=\"" << localname << "\"";
  query.execute();
}
