#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGAlgQueue.h"
#include "CGAlg.h"
#include "CGJob.h"

#include <map>
#include <uuid/uuid.h>

using namespace std;

CGAlgQueue::CGAlgQueue(CGAlg &alg):type(alg)
{
    jobs.clear();
}

CGAlgQueue::~CGAlgQueue()
{
}

vector<uuid_t *> *CGAlgQueue::add(vector<CGJob &> &jobs)
{
    return NULL;
}

uuid_t *CGAlgQueue::add(CGJob *job)
{
    uuid_t *id = new uuid_t[1];
    uuid_generate(*id);
    CGJob *tJob = job;
    tJob->setStatus(CG_INIT);
    jobs.insert(pair<uuid_t *, CGJob *>(id, tJob));
    return id;
}

void CGAlgQueue::remove(vector<uuid_t *> *ids)
{
  for (vector<uuid_t *>::iterator it = ids->begin(); it != ids->end(); it++)
    remove(*it);
}

void CGAlgQueue::remove(uuid_t *id)
{
    delete jobs[id];
    jobs.erase(id);
    
}

CGJobStatus CGAlgQueue::getStatus(uuid_t *id)
{
    CGJob *job = jobs[id];
    CGJobStatus aStat = job->getStatus();
//    nStat = aStat;
//    switch (aStat) {
//	case CG_INIT:
//	    nStat = CG_SUBMITTED;
//	    break;
//	case CG_SUBMITTED:
//	    nStat = CG_RUNNING;
//	    break;
//	case CG_RUNNING:
//	    nStat = CG_FINISHED;
//	    break;
//    }
//    job->setStatus(nStat);
    return aStat;
}
