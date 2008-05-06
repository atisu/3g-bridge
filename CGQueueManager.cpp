#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGQueueManager.h"
#include "CGSqlStruct.h"

#include <map>
#include <list>
#include <string>
#include <iostream>
#include <sstream>
#include <uuid/uuid.h>
#include <mysql++/mysql++.h>
#include <sys/stat.h>

using namespace std;
using namespace mysqlpp;

CGQueueManager::CGQueueManager(char *conf, char *db, char *host, char *user, char *passwd)
{
    // Store base directory
    basedir = string(getcwd(NULL, 0));
    
    // Clear algorithm list
    algs.clear();

#ifdef HAVE_DCAPI
    gridHandlers[CG_ALG_DCAPI] = new DCAPIHandler(conf, basedir);
#endif
#ifdef HAVE_EGEE
    gridHandlers[CG_ALG_EGEE] = new EGEEHandler(conf);
#endif

    try {
	con.connect(db, host, user, passwd);
    } catch (exception& ex) {
	cerr << ex.what() << endl;
	throw -10;
    }
}

CGQueueManager::~CGQueueManager()
{
    for (map<string, CGAlgQueue *>::iterator it = algs.begin(); it != algs.end(); it++)
	delete it->second;
}

/*
 * Add an algorithm - create an algorithm queue
 */
bool CGQueueManager::addAlg(CGAlg &what)
{
    for (map<string, CGAlgQueue *>::iterator it = algs.begin(); it != algs.end(); it++)
	if (it->first == what.getName())
	    return false;
    CGAlgQueue *algQ = new CGAlgQueue(what);
    algs.insert(pair<string, CGAlgQueue *>(what.getName(), algQ));
    return true;
}


/**
 * Query jobs from DB. This function queries jobs having the given status
 * and returns them in a vector.
 *
 * @param[in] stat Status to query
 * @return Vector of jobs having the given status
 */
vector<CGJob *> *CGQueueManager::query_jobs(CGJobStatus stat)
{
    vector<CGJob *> *jobs = new vector<CGJob *>();
    string statStr;
    Query query = con.query();
    switch (what) {
	case CG_INIT:
	    statStr = "CG_INIT";
	    break;
	case CG_RUNNING:
	    statStr = "CG_RUNNING";
	    break;
	case CG_FINISHED:
	    statStr = "CG_FINISHED";
	    break;
	case CG_ERROR:
	    statStr = "CG_ERROR";
	    break;
	default:
	    return jobs;
	    break;
    }

    // Query jobs with specified status
    query << "SELECT * FROM cg_job WHERE status = \"" << statStr << "\"";
    vector<cg_job> newJobs;
    query.storein(newJobs);

    // Create result vector out of query results
    for (vector<cg_job>::iterator it = newJobs.begin(); it != newJobs.end(); it++) {
	int id;
	string name;
	string cmdlineargs;
	string token;
	string algname;
	string wuid;

    	id = it->id;
	name = it->name;
	cmdlineargs = it->args;
	algname = it->algname;
	wuid = it->wuid;

	// Vectorize cmdlineargs string
        list<string> *arglist = new list<string>();
	istringstream iss(cmdlineargs);
	while (getline(iss, token, ' '))
	    arglist->push_back(token);

        // Find out which algorithm the job belongs to
	map<string, CGAlgQueue *>::iterator at = algs.find(algname);
	//!!!! What happens if...?
	if (at == algs.end())
	    return jobs;
	CGAlg *alg = at->second->getType();

	// Create new job descriptor
        CGJob *nJob = new CGJob(name, arglist, *alg);

        // Get inputs for job from db
	query.reset();
	query << "SELECT * FROM cg_inputs WHERE jobid = " << id;
	vector<cg_inputs> in;
	query.storein(in);
	for (vector<cg_inputs>::iterator it = in.begin(); it != in.end(); it++)
	    nJob->addInput(it->localname, it->path);

	// Get outputs for job from db
	query.reset();
	query << "SELECT * FROM cg_outputs WHERE jobid = " << id;
	vector<cg_outputs> out;
	query.storein(out);
	for (vector<cg_outputs>::iterator it = out.begin(); it != out.end(); it++)
	    nJob->addOutput(string(it->localname));

	// Also register stdout and stderr
	nJob->addOutput("stdout.txt");
	nJob->addOutput("stderr.txt");

	jobs->push_back(nJob);
    }
    return jobs;
}


/**
 * Handle jobs. This function handles selected jobs. The performed operation
 * depends on the given op. For successful operations, the job objects are
 * updated.
 *
 * @param[in] op The operation to perform
 * @param[in,out] jobs Pointer to vector of jobs to perform the operation on
 */
void CGQueueManager::handleJobs(jobOperation op, vector<CGJob *> *jobs)
{
  map<CGAlgType, vector<CGJob *> > gridMap;
  // Create a map of algorithm (grid) types to jobs
  for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++) {
    CGAlgType actType = it->getType()->getType();
    gridMap[actType].push_back(it);
  }

  // Use the selected grid plugin for submission
  for (CGAlgType c = CG_ALG_MIN; c != CG_ALG_MAX; c = CGAlgType(c+1)) {
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
    case abort:
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
	vector<CGJob *> *newJobs = query_jobs(CG_INIT);
	vector<CGJob *> *sentJobs = query_sent_jobs(CG_RUNNING);
	vector<CGJob *> *finishedJobs = query_finished_jobs(CG_FINISHED);
	vector<CGJob *> *abortedJobs = query_aborted_jobs(CG_ERROR);
	handleJobs(submit, newJobs);
	handleJobs(status, sentJobs);
	handleJobs(output, finishedJobs);
	handleJobs(abort, abortedJobs);
	freeVector(newJobs);
	freeVector(sentJobs);
	freeVector(finishedJobs);
	freeVector(abortedJobs);
	sleep(300);
    }
}
