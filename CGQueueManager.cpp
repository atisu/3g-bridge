#include "CGQueueManager.h"
#include "CGSqlStruct.h"

#include <map>
#include <list>
#include <string>
#include <iostream>
#include <sstream>
#include <uuid/uuid.h>
#include <mysql++.h>

using namespace std;

CGQueueManager::CGQueueManager(char *dcapi_conf, char *db, char *host, char *user, char *passwd)
{
    algs.clear();
    
    if (DC_OK != DC_initMaster(dcapi_conf)) {
	throw DC_initMasterError;
    }
    
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

/*
 * Add jobs 
 */
vector<uuid_t *> *CGQueueManager::addJobs(vector<CGJob *> &jobs)
{
    vector<uuid_t *> *IDs = new vector<uuid_t *>();
    for (vector<CGJob *>::iterator it = jobs.begin(); it != jobs.end(); it++)
	IDs->push_back(addJob(**it));
    return IDs;
}

/*
 * Add a job to the adequate algorithm queue
 */
uuid_t *CGQueueManager::addJob(CGJob &job)
{
    uuid_t *ret;
    DC_Workunit *wu;
    char *tag = new char[37];

    CGAlgQueue *aq = algs[job.getType()->getName()];
    vector<string> inputs = job.getInputs();
    vector<string> outputs = job.getOutputs();
    string localname;
    string inputpath;
    CGAlg *type = job.getType();
    string algName = type->getName();

    ret = aq->add(&job);
    
    // Add job ID
    jobIDs.insert(ret);

    // Add job ID -> AlgQ mapping
    ID2AlgQ.insert(pair<uuid_t *, CGAlgQueue *>(ret, algs[job.getType()->getName()]));

    // Add job id to wu_tag
    uuid_unparse(*ret, tag);

    // Get command line parameters and create null-terminated list
    list<string> *arglist = job.getArgv();
    int i = 0, size = arglist->size() + 1;
    const char *argv[size];
    for (list<string>::iterator it = arglist->begin(); it != arglist->end(); it++) {
	int arglength = it->length();
	const char *argtoken = new char[arglength];
	argtoken = it->c_str();
	argv[i] = argtoken;
	i++;
    }
    argv[i] = NULL;

    // Create WU descriptor
    wu = DC_createWU(algName.c_str(), argv, 0, tag);
    // Free allocated c strings
    delete [] tag;
    for (i = size - 1; i = 0; i--) {
	delete [] argv[i];
    }

    if (!wu) {
	throw DC_createWUError;
    }

    // Register WU inputs
    for (vector<string>::iterator it = inputs.begin(); it != inputs.end(); it++) {
	inputpath = job.getInputPath(localname = *it);
	
        if (DC_addWUInput(wu, localname.c_str(), inputpath.c_str(), DC_FILE_PERSISTENT)) {
	    throw DC_addWUInputError;
	}
    }

    //Register WU outputs
    for (vector<string>::iterator it = outputs.begin(); it != outputs.end(); it++) {
        localname = *it;
	if (DC_addWUOutput(wu, localname.c_str())) {
	    throw DC_addWUOutputError;
	}
    }
    
    // Submit WU
    if (DC_submitWU(wu)) {
	throw DC_submitWUError;
    }

    // Set status of job to RUNNING
    job.setStatus(CG_RUNNING);
    
    // Serialize WU and set the wuID of the job entity
    job.setWUId(DC_serializeWU(wu));

    return ret;
}

/*
 * Remove jobs
 */
void CGQueueManager::removeJobs(const vector<uuid_t *> &ids)
{
    for (vector<uuid_t *>::const_iterator it = ids.begin(); it != ids.end(); it++)
	removeJob(*it);
}

/*
 * Remove a job from the adequate algorithm queue
 */
void CGQueueManager::removeJob(uuid_t *id)
{
    CGJob *job = ID2AlgQ[id]->getJob(id);

    DC_Workunit *wu = DC_deserializeWU(job->getWUId());
    wu = DC_deserializeWU(job->getWUId());
    DC_cancelWU(wu);
    
    ID2AlgQ[id]->remove(id);	// Remove job from AlgQ
    ID2AlgQ.erase(id);		// Remove ID->AlgQ mapping entity
    jobIDs.erase(id);		// Remove job ID
}

/*
 * Get status of jobs
 */
vector<CGJobStatus> *CGQueueManager::getStatuses(vector<uuid_t *> &ids)
{
    vector<CGJobStatus> *stats = new vector<CGJobStatus>();
    for (vector<uuid_t *>::iterator it = ids.begin(); it != ids.end(); it++)
	stats->push_back(getStatus(*it));
    return stats;
}

/*
 * Get status of a job
 */
CGJobStatus CGQueueManager::getStatus(uuid_t *id)
{
    return ID2AlgQ[id]->getStatus(id);
}

/*
 * Query database for any returning results 
 * and set output paths of jobs, if ready
 */
void CGQueueManager::query(int timeout)
{
    DC_MasterEvent *event;
    static DC_Workunit *wu;
    DC_Result *result;
    char *outfilename, *wutag;
    uuid_t *jobid;
    CGJob *job;
    vector<string> outputs;
    string localname;
    
    event = DC_waitMasterEvent(NULL, timeout);

    if (event && event->type == DC_MASTER_RESULT) {
	wutag = DC_getWUTag(wu);
	uuid_parse(wutag, *jobid);
	job = ID2AlgQ[jobid]->getJob(jobid);
    
	if (!event->result) {
	    cerr << "Job with id " << wutag << " has failed" << endl;
	    job->setStatus(CG_ERROR);
	    DC_destroyWU(wu);
	    return;
	}
    
	// Retreive paths of result
	outputs = job->getOutputs();
	for (vector<string>::iterator it = outputs.begin(); it != outputs.end(); it++) {
	    localname = *it;
	    
	    outfilename = DC_getResultOutput(event->result, localname.c_str());
	    if (!outfilename) {
	    	cerr << "No output for job with id " << wutag << endl;
    		job->setStatus(CG_ERROR);
    		DC_destroyWU(wu);
		return;
	    }
	    job->setOutputPath(localname, outfilename);
	}
	job->setStatus(CG_FINISHED);
	
	DC_destroyWU(wu);
    }
    DC_destroyMasterEvent(event);
}

/*
 * Call this periodically to check for new jobs to be submitted
 */
vector<CGJob *> *CGQueueManager::getJobsFromDb() {
    int id, i = 0;
    string name;
    string cmdlineargs;
    string token;
    string algname;
    mysqlpp::Query query = con.query();
    vector<CGJob *> *jobs = new vector<CGJob *>();

    // select new jobs from db, let mysql filter jobs already queued
    query << "SELECT * FROM cg_job WHERE name NOT IN (";
    string oldjobs = "";
    for (map<string, CGAlgQueue *>::iterator it = algs.begin(); it != algs.end(); it++)
    {
	CGAlgQueue *aq = it->second;
	map<uuid_t *, CGJob *> jobs = aq->getJobs();
	for(map<uuid_t *, CGJob *>::iterator at = jobs.begin(); at != jobs.end(); at++)
	{
	    oldjobs += "\"" + at->second->getName() + "\", ";
	}
    }
    oldjobs += "\"\"";
    query << oldjobs << ")";
    vector<cg_job> job;
    query.storein(job);
    
    for (vector<cg_job>::iterator it = job.begin(); it != job.end(); it++) {
    
	id = it->id;
	name = it->name;
	cmdlineargs = it->args;
	algname = it->algname;
	
	// Vectorize cmdlineargs string
        list<string> *arglist = new list<string>();
	istringstream iss(cmdlineargs);
	while (getline(iss, token, ' '))
	    arglist->push_back(token);
	
        // Find out which algorithm the job belongs to
	map<string, CGAlgQueue *>::iterator at = algs.find(algname);
	if (at == algs.end()) return jobs;
	CGAlg *alg = at->second->getType();

	// Create new job descriptor	
        CGJob *nJob = new CGJob(name, arglist, *alg);

        // Get inputs for job from db
	query.reset();
	query << "select * from cg_inputs where jobid = " << id;
	vector<cg_inputs> in;
	query.storein(in);
	for (vector<cg_inputs>::iterator init = in.begin(); init != in.end(); init++)
	    nJob->addInput(init->localname, init->path);
		
	// Get outputs for job from db
	query.reset();
	query << "select * from cg_outputs where jobid = " << id;
	vector<cg_outputs> out;
	query.storein(out);
	for (vector<cg_outputs>::iterator outit = out.begin(); outit != out.end(); outit++)
	    nJob->addOutput(outit->localname);

	jobs->push_back(nJob);
    }
    return jobs;
}
