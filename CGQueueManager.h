#ifndef __CGQUEUEMANAGER_H
#define __CGQUEUEMANAGER_H

#include "CGAlg.h"
#include "CGJob.h"
#include "common.h"
#include "CGAlgQueue.h"
#include "GridHandler.h"

#include <dc.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <uuid/uuid.h>
#include <mysql++/mysql++.h>

using namespace std;
using namespace mysqlpp;

enum jobOperation {
  submit,
  status,
  output,
  abort
};

class CGQueueManager {
public:
    CGQueueManager(char *conf, char *db, char *host, char *user, char *passwd);
    ~CGQueueManager();
    bool addAlg(CGAlg &what);
    void run();
private:
    map<string, CGAlgQueue *> algs;
    set<uuid_t *> jobIDs;
    map<uuid_t *, CGAlgQueue *> ID2AlgQ;
    Connection con;
    string basedir;
    map<CGAlgType, GridHandler *> gridHandlers;
    void handleJobs(jobOperation op, vector<CGJob *> *jobs);
    vector<CGJob *> *query_jobs(CGJobStatus stat);
    void freeVector(vector<CGJob *> *what);
};

#endif  /* __CGQUEUEMANAGER_H */
