#ifndef __JOBDB_H
#define __JOBDB_H

#include <string>
#include "CGJob.h"
#include "CGAlgQueue.h"

#include <string>
#include <mysql++/mysql++.h>
#ifdef HAVE_MYSQL___SSQLS_H
#include <mysql++/ssqls.h>
#else
#include <mysql++/custom.h>
#endif

using namespace std;
using namespace mysqlpp;

class JobDB {
 public:
  JobDB(const string host, const string user, const string passwd, const string dbname);
  ~JobDB();
  vector<CGJob *> *getJobs(CGJobStatus stat);
  vector<CGJob *> *getJobs(string gridID);
  void addJobs(vector<CGJob *> *jobs);
  void updateJobGridID(string ID, string gridID);
  void updateJobStat(string gridID, CGJobStatus newstat);
  void setAlgQs(map<string, CGAlgQueue *> *newone) { talgs = newone; }
 private:
  string getStatStr(CGJobStatus stat);
  vector<CGJob *> *parseJobs(Query *squery);
  map<string, CGAlgQueue *> *talgs;
  string thost;
  string tuser;
  string tpasswd;
  string tdbname;
  Connection *conn;
};

#endif
