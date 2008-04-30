#ifndef __CGQUEUEMANAGER_H
#define __CGQUEUEMANAGER_H

#include "CGAlg.h"
#include "CGJob.h"
#include "common.h"
#include "CGAlgQueue.h"

#include <dc.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <uuid/uuid.h>
#include <mysql++/mysql++.h>

using namespace std;
using namespace mysqlpp;

class CGQueueManager {
public:
    CGQueueManager(char *dcapi_conf, char *db, char *host, char *user, char *passwd);
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
private:
    map<string, CGAlgQueue *> algs;
    set<uuid_t *> jobIDs;
    map<uuid_t *, CGAlgQueue *> ID2AlgQ;
    Connection con;
    string basedir;
};

#endif  /* __CGQUEUEMANAGER_H */
