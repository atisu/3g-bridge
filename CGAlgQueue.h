#ifndef __CG_ALGQUEUE_H
#define __CG_ALGQUEUE_H

#include "CGAlg.h"
#include "CGJob.h"
#include "common.h"

#include <map>
#include <uuid/uuid.h>

using namespace std;

class CGAlgQueue {
public:
    CGAlgQueue(CGAlg &alg);
    ~CGAlgQueue();
    vector<uuid_t *> *add(vector<CGJob &> &jobs);
    uuid_t *add(CGJob *job);
    void remove(vector<uuid_t *> *ids);
    void remove(uuid_t *id);
    CGJob *getJob(uuid_t *id) { return jobs[id]; }
    CGJobStatus getStatus(uuid_t *id);
    CGAlg getType() { return type; }
private:
    CGAlg type;
    map<uuid_t *, CGJob *> jobs;
};

#endif  /* __CG_ALGQUEUE_H */
