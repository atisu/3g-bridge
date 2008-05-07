#ifndef __JOBDB_H
#define __JOBDB_H

#include "CGJob.h"

#include <string>
#include <mysql++/mysql++.h>

using namespace std;

class JobDB {
 public:
  JobDB(const string host, const string user, const string passwd, const string dbname);
  ~JobDB();
  vector<CGJob *> *getJobs(CGJobStatus stat);
  vector<CGJob *> *getJobs(string gridID);
  void addJobs(vector<CGJob *> *jobs);
  void updateJobStat(string gridID, CGJobStatus newstat);
  void setAlgQs(map<string, CGAlgQueue *> *new) { talgs = new; }
 private:
  vector<CGJob *> *parseJobs(vector<cg_job> *source);
  map<string, CGAlgQueue *> *talgs;
  string thost;
  string tuser;
  string tpasswd;
  string tdbname;
  Connection con;
};

#endif
