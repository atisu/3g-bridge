#ifndef __CG_ALGQUEUE_H
#define __CG_ALGQUEUE_H

#include "CGAlg.h"
//#include "CGJob.h"
#include "common.h"

#include <vector>
//#include <uuid/uuid.h>

using namespace std;

class CGAlgQueue {
public:
    static vector<CGAlgQueue *> queues;
    //~CGAlgQueue();
    //vector<uuid_t *> *add(vector<CGJob &> &jobs);
    //uuid_t *add(CGJob *job);
    //void remove(vector<uuid_t *> *ids);
    //void remove(uuid_t *id);
    //CGJob *getJob(uuid_t *id) { return jobs[id]; }
    //map<uuid_t *, CGJob *>getJobs() { return jobs; }
    //CGJobStatus getStatus(uuid_t *id);
    CGAlgType getType() { return ttype; }
    string getName() { return tname; }
    static CGAlgQueue *getInstance(const CGAlgType &type, const string &algName);
    static void cleanUp();
private:
    CGAlgQueue(const CGAlgType &type, const string &algName);
    CGAlgType ttype;
    string tname;
    //map<uuid_t *, CGJob *> jobs;
};

#endif  /* __CG_ALGQUEUE_H */
