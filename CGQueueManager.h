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
#include <mysql++.h>

using namespace std;

class CGQueueManager {
public:
    CGQueueManager(char *dcapi_conf, char *db, char *host, char *user, char *passwd);
    ~CGQueueManager();
    bool addAlg(CGAlg &what);
    vector<uuid_t *> *addJobs(vector<CGJob *> &jobs);
    uuid_t *addJob(CGJob &job);
    void removeJobs(const vector<uuid_t *> &ids);
    void removeJob(uuid_t *id);
    vector<CGJobStatus> *getStatuses(vector<uuid_t *> &ids);
    CGJobStatus getStatus(uuid_t *id);
    void query(int timeout = 5);
    vector<uuid_t *> *getJobsFromDb();
private:
    map<string, CGAlgQueue *> algs;
    set<uuid_t *> jobIDs;
    map<uuid_t *, CGAlgQueue *> ID2AlgQ;
    mysqlpp::Connection con;
};

#endif  /* __CGQUEUEMANAGER_H */
