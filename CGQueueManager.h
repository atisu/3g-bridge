#ifndef __CGQUEUEMANAGER_H
#define __CGQUEUEMANAGER_H

#include "CGJob.h"
#include "DBHandler.h"
#include "common.h"
#include "CGAlgQueue.h"
#include "GridHandler.h"
#include "QMConfig.h"

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
  cancel
};

class CGQueueManager {
public:
    CGQueueManager(QMConfig &config);
    ~CGQueueManager();
    void run();
private:
    DBHandler *jobDB;
    map<string, CGAlgQueue *> algs;
    set<uuid_t *> jobIDs;
    map<uuid_t *, CGAlgQueue *> ID2AlgQ;
    string basedir;
    map<CGAlgType, GridHandler *> gridHandlers;
    unsigned selectSize(CGAlgQueue *algQ);
    unsigned selectSizeAdv(CGAlgQueue *algQ);
    void handlePackedSubmission(GridHandler *gh, vector<CGJob *> *jobs);
    void schedReq(GridHandler *gh, vector<CGJob *> *jobs);
    void handleJobs(jobOperation op, vector<CGJob *> *jobs);
    void freeVector(vector<CGJob *> *what);
};

#endif  /* __CGQUEUEMANAGER_H */
