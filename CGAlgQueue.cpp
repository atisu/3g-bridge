#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGAlgQueue.h"
//#include "CGAlg.h"
//#include "CGJob.h"

//#include <map>
#include <iostream>
//#include <uuid/uuid.h>


using namespace std;


CGAlgQueue::CGAlgQueue(const CGAlgType &type, const string &name):ttype(type),tname(name)
{
}


void CGAlgQueue::cleanUp()
{
    for (unsigned i = 0; i < queues.size(); i++)
	delete queues[i];
}

/*
vector<uuid_t *> *CGAlgQueue::add(vector<CGJob &> &jobs)
{
    return NULL;
}

uuid_t *CGAlgQueue::add(CGJob *job)
{
    uuid_t *id = new uuid_t[1];
    uuid_generate(*id);
    CGJob *tJob = job;
    tJob->setStatus(INIT);
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
    return aStat;
}
*/


/*CGAlgQueue *CGAlgQueue::getInstance(const CGAlg &alg)
{
    for (unsigned i = 0; i < queues.size(); i++)
    {
	CGAlg *actA = queues[i]->getAlgorithm();
	if (actA->getName() == alg.getName() && actA->getType() == alg.getType())
	    return queues[i];
	return new CGAlgQueue(alg);
    }
}
*/

CGAlgQueue *CGAlgQueue::getInstance(const CGAlgType &type, const string &algName)
{
    for (unsigned i = 0; i < queues.size(); i++)
    {
	if (queues[i]->getName() == algName && queues[i]->getType() == type)
	    return queues[i];
    }
    CGAlgQueue *rv = new CGAlgQueue(type, algName);
    queues.push_back(rv);
    return rv;
}
