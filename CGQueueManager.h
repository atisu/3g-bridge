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
    vector<uuid_t *> *addJobs(vector<CGJob *> &jobs, bool start = false);
    uuid_t *addJob(CGJob &job, bool start = false);
    void removeJobs(const vector<uuid_t *> &ids);
    void removeJob(uuid_t *id);
    vector<CGJobStatus> *getStatuses(vector<uuid_t *> &ids);
    CGJobStatus getStatus(uuid_t *id);
    void queryBoinc(int timeout = 5);
    vector<CGJob *> *getJobsFromDb(bool start = false);
    void putOutputsToDb();
    void registerWuOfJob(uuid_t *id, CGJob &job);
    void registerWuOfJobs(vector<CGJob *> &jobs);
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
