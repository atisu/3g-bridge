#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGQueueManager.h"

#include <map>
#include <list>
#include <string>
#include <iostream>
#include <sstream>
#include <uuid/uuid.h>
#include <sys/stat.h>

#ifdef HAVE_DCAPI
#include "DCAPIHandler.h"
#endif
#ifdef HAVE_EGEE
#include "EGEEHandler.h"
#endif

using namespace std;
using namespace mysqlpp;


/**
 * Constructor. Initialize selected grid plugin, and database connection.
 */
CGQueueManager::CGQueueManager(const string conf, const string db, const string host, const string user, const string passwd)
{
  // Clear algorithm list
  algs.clear();

  jobDB = new JobDB(host, user, passwd, db);
  
#ifdef HAVE_DCAPI
  gridHandlers[CG_ALG_DCAPI] = new DCAPIHandler(conf);
#endif
#ifdef HAVE_EGEE
  gridHandlers[CG_ALG_EGEE] = new EGEEHandler(jobDB, conf);
#endif
}


/**
 * Destructor. Frees up memory.
 */
CGQueueManager::~CGQueueManager()
{
  for (map<string, CGAlgQueue *>::iterator it = algs.begin(); it != algs.end(); it++)
    delete it->second;

#ifdef HAVE_DCAPI
  delete gridHandlers[CG_ALG_DCAPI];
#endif
#ifdef HAVE_EGEE
  delete gridHandlers[CG_ALG_EGEE];
#endif

  delete jobDB;
}


/**
 * Add an algorithm - create an algorithm queue
 */
bool CGQueueManager::addAlg(CGAlg &what)
{
  for (map<string, CGAlgQueue *>::iterator it = algs.begin(); it != algs.end(); it++)
    if (it->first == what.getName())
      return false;
  CGAlgQueue *algQ = new CGAlgQueue(what);
  algs.insert(pair<string, CGAlgQueue *>(what.getName(), algQ));
  jobDB->setAlgQs(&algs);
  return true;
}


/**
 * Handle jobs. This function handles selected jobs. The performed operation
 * depends on the given op. For successful operations, the job enties in the
 * DB are updated
 *
 * @param[in] op The operation to perform
 * @param[in,out] jobs Pointer to vector of jobs to perform the operation on
 */
void CGQueueManager::handleJobs(jobOperation op, vector<CGJob *> *jobs)
{
  map<CGAlgType, vector<CGJob *> > gridMap;
  // Create a map of algorithm (grid) types to jobs
  for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++) {
    CGAlgType actType = (*it)->getAlgorithm()->getType();
    gridMap[actType].push_back(*it);
  }

  // Use the selected grid plugin for submission
  for (CGAlgType c = CG_ALG_MIN; c != CG_ALG_MAX; c = CGAlgType(c+1)) {
    if (!gridHandlers[c])
      continue;
    switch (op) {
    case submit:
      gridHandlers[c]->submitJobs(&(gridMap[c]));
      break;
    case status:
      gridHandlers[c]->getStatus(&(gridMap[c]));
      break;
    case output:
      gridHandlers[c]->getOutputs(&(gridMap[c]));
      break;
    case cancel:
      gridHandlers[c]->cancelJobs(&(gridMap[c]));
      break;
    }
  }
}


/**
 * Free vectors. This function releases memory space allocated that is not
 * needed anymore.
 *
 * @param[in] what Pointer to vector to free up
 */
void CGQueueManager::freeVector(vector<CGJob *> *what)
{
  for (vector<CGJob *>::iterator it = what->begin(); it != what->end(); it++)
    delete *it;
  delete what;
}


/**
 * Main loop. Periodically queries database for new, sent, finished and
 * aborted jobs. Handler funcitons are called for the different job vectors.
 */
void CGQueueManager::run()
{
  bool finish = false;
  while (!finish) {
    vector<CGJob *> *newJobs = jobDB->getJobs(INIT);
    vector<CGJob *> *sentJobs = jobDB->getJobs(RUNNING);
    vector<CGJob *> *finishedJobs = jobDB->getJobs(FINISHED);
    vector<CGJob *> *abortedJobs = jobDB->getJobs(ERROR);
    handleJobs(submit, newJobs);
    handleJobs(status, sentJobs);
    handleJobs(output, finishedJobs);
    handleJobs(cancel, abortedJobs);
    finish = (newJobs->size() == 0 && sentJobs->size() == 0);
    freeVector(newJobs);
    freeVector(sentJobs);
    freeVector(finishedJobs);
    freeVector(abortedJobs);
    sleep(30);
  }
}
