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
    gridHandlers[CG_ALG_DCAPI] = new DCAPIHandler(conf);
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

/*
 * Add jobs 
 */
vector<uuid_t *> *CGQueueManager::addJobs(vector<CGJob *> &jobs, bool start)
{
    vector<uuid_t *> *IDs = new vector<uuid_t *>();
    for (vector<CGJob *>::iterator it = jobs.begin(); it != jobs.end(); it++)
	IDs->push_back(addJob(**it, start));
    return IDs;
}

/*
 * Add a job to the adequate algorithm queue
 */
uuid_t *CGQueueManager::addJob(CGJob &job, bool start)
{
    CGAlgQueue *aq = algs[job.getType()->getName()];
    uuid_t *ret = aq->add(&job);

    // Add job ID
    jobIDs.insert(ret);

    // Add job ID -> AlgQ mapping
    ID2AlgQ.insert(pair<uuid_t *, CGAlgQueue *>(ret, algs[job.getType()->getName()]));

    // If this is a new job, create a WU of it
    if (!start) 
        registerWuOfJob(ret, job);

    return ret;
}

void CGQueueManager::registerWuOfJob(uuid_t *id, CGJob &job)
{
    DC_Workunit *wu;
    char *tag = new char[37];
    Query query = con.query();

    vector<string> inputs = job.getInputs();
    vector<string> outputs = job.getOutputs();
    string localname, inputpath;
    CGAlg *type = job.getType();
    string algName = type->getName();
    
    // Add job id to wu_tag
    uuid_unparse(*id, tag);

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
	// Do not register stdout and stderr as inputs
	if (localname.compare("stdout.txt") && localname.compare("stderr.txt"))
	    if (DC_addWUOutput(wu, localname.c_str()))
		throw DC_addWUOutputError;
    }
    
    // Submit WU
    if (DC_submitWU(wu)) {
	throw DC_submitWUError;
    }

    // Serialize WU and set the wuID of the job entity
    job.setGridId(DC_serializeWU(wu));

    // Set status of job to CG_RUNNING
    job.setStatus(CG_RUNNING);
    string name = job.getName();
    query.reset();
    query << "UPDATE cg_job SET status = \"CG_RUNNING\", wuid = \"" << string(job.getGridId()) << "\" WHERE name = \"" << name << "\"";
    query.execute();
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

    // If the job is still running, tell Boinc to cancel the job
    if (job->getStatus() == CG_RUNNING) {
        DC_Workunit *wu = DC_deserializeWU(job->getGridId().c_str());
        DC_cancelWU(wu);
    }
    
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
 * Query boinc database for any returning results 
 * and set output paths of jobs, if ready
 */
void CGQueueManager::queryBoinc(int timeout)
{
    CGJob *job = NULL;
    DC_MasterEvent *event;
    char *wutag;
    uuid_t *id = new uuid_t[1];
    vector<string> outputs;
    string localname, outfilename, algname, workdir;
    struct stat stFileInfo;
    
    event = DC_waitMasterEvent(NULL, timeout);

    // If we have a result
    if (event && event->type == DC_MASTER_RESULT) {
	wutag = DC_getWUTag(event->wu);
	if(uuid_parse(wutag, *id) == -1) {
	    cerr << "Failed to parse uuid" << endl;
	    DC_destroyWU(event->wu);
	    DC_destroyMasterEvent(event);
	    return;
	}

        // Stupid UUID_T, "MMffm mmfmm mmmfmmm" - Kenny, South Park
	for (set<uuid_t *>::iterator it = jobIDs.begin(); it != jobIDs.end(); it++) {
	    uuid_t * p = *it;
	    if (uuid_compare(*p, *id) == 0) {
		job = ID2AlgQ[*it]->getJob(*it);
		algname = ID2AlgQ[*it]->getType()->getName();
	    }
        }
	free(id);
	
	// If we could not find the corresponding job
	if (!job) {
	    cerr << "Could not find job!" << endl;
	    DC_destroyWU(event->wu);
	    DC_destroyMasterEvent(event);
	    return;
	}
	    
        if (!event->result) {
    	    cerr << "Job with id " << wutag << " has failed" << endl;
	    job->setStatus(CG_ERROR);
	    DC_destroyWU(event->wu);
	    DC_destroyMasterEvent(event);
	    return;
	}
        // Retreive paths of output, stdout and stderr
	workdir = basedir;
	workdir += "/workdir/";
	workdir += algname;
	workdir += "/";
	workdir += wutag;
	workdir += "/";
	
	// Retreive list of outputs
        outputs = job->getOutputs();
	
	// Create output directory if there is an output
	string cmd = "mkdir -p ";
	cmd += workdir;
	system(cmd.c_str());

	for (vector<string>::iterator it = outputs.begin(); it != outputs.end(); it++) {
	    localname = *it;
	    if (localname.compare("stdout.txt") == 0) {
		if (stat(DC_getResultOutput(event->result, DC_LABEL_STDOUT), &stFileInfo) == 0) {
	    	    cmd = "cp ";
		    cmd += string(DC_getResultOutput(event->result, DC_LABEL_STDOUT));
		    cmd += " ";
		    cmd += workdir;
		    cmd += "stdout.txt";
		    system(cmd.c_str());
		    outfilename = workdir;
		    outfilename += "stdout.txt";
		}
	    } else if (localname.compare("stderr.txt") == 0) {
		if (stat(DC_getResultOutput(event->result, DC_LABEL_STDERR), &stFileInfo) == 0) {
		    cmd = "cp ";
		    cmd += string(DC_getResultOutput(event->result, DC_LABEL_STDERR));
		    cmd += " ";
		    cmd += workdir;
		    cmd += "stderr.txt";
		    system(cmd.c_str());
		    outfilename = workdir;
		    outfilename += "stderr.txt";
		}
	    } else {
		if (stat(DC_getResultOutput(event->result, localname.c_str()), &stFileInfo) == 0) {
		    cmd = "cp ";
		    cmd += string(DC_getResultOutput(event->result, localname.c_str()));
		    cmd += " ";
		    cmd += workdir;
		    cmd += localname;
		    system(cmd.c_str());
		    outfilename = workdir;
		    outfilename += localname;
		} else {
    	    	    cerr << "No output for job with id " << wutag << endl;
		    cmd = "rm -r ";
		    cmd += workdir;
		    system(cmd.c_str());
	    	    job->setStatus(CG_ERROR);
		    DC_destroyWU(event->wu);
		    DC_destroyMasterEvent(event);
    	    	    return;
		}
	    }
	    job->setOutputPath(localname, outfilename);
	}
	// Set status of job to finished
        job->setStatus(CG_FINISHED);
	// Destroy the wu as it is no longer needed
	DC_destroyWU(event->wu);
    }
    DC_destroyMasterEvent(event);
}

/*
 * Iterate all jobs and insert the paths of outputs
 * if the status of the job is finished
 */

void CGQueueManager::putOutputsToDb() {
    Query query = con.query();
    int id;
    
    for (map<uuid_t *, CGAlgQueue *>::iterator it = ID2AlgQ.begin(); it != ID2AlgQ.end(); it++) {
	map<uuid_t *, CGJob *> jobs = it->second->getJobs();
	for (map<uuid_t *, CGJob *>::iterator at = jobs.begin(); at != jobs.end(); at++) {
	    // The job to work with
	    CGJob *job = at->second;
	    // The id of the job (needed for job remova)
	    uuid_t *jobId = at->first;
	    query.reset();
	    query << "SELECT * FROM cg_job WHERE name = \"" << job->getName() << "\"";
    	    vector<cg_job> s_job;
	    query.storein(s_job);
	    id = s_job.at(0).id;
	    string status = s_job.at(0).status;

	    if (job->getStatus() == CG_FINISHED) {
		// Select only jobs that are in CG_RUNNING state
		if (status.compare("CG_RUNNING") == 0) {
		    // Put stdout and stderr rows in cg_outputs, 
		    // before setting path values
    	    	    cg_outputs row_1(0, "stdout.txt", "", id);
		    query.reset();
		    query.insert(row_1);
		    query.execute();
		    cg_outputs row_2(0, "stderr.txt", "", id);
		    query.reset();
		    query.insert(row_2);
		    query.execute();
		    
    	    	    // Update the path fields of cg_outputs table
		    query.reset();
		    query << "SELECT * FROM cg_outputs WHERE jobid = " << id;
		    vector<cg_outputs> outputs;
		    query.storein(outputs);
		    for (vector<cg_outputs>::iterator it = outputs.begin(); it != outputs.end(); it++) {
			query.reset();
			query << "UPDATE cg_outputs SET path = \"" 
			      << job->getOutputPath(it->localname)
			    << "\" WHERE jobid = " << id 
			    << " AND localname = \"" << it->localname << "\"";
			query.execute();
		    }
		    // Set status of job to CG_FINISHED
		    query << "UPDATE cg_job SET status = \"CG_FINISHED\" WHERE id = " << id; 
		    query.execute();
		    // Remove job from queue
		    removeJob(jobId);
		}
	    } else if (job->getStatus() == CG_ERROR) {
		    // Set status of failed job to CG_ERROR
		    query << "UPDATE cg_job SET status = \"CG_ERROR\" WHERE id = " << id;
		    query.execute();
		    
		    // Delete job, it's not our problem from here
		    removeJob(jobId);
		    
	    }
	}
    }
}

/*
 * Call this periodically to check for new jobs to be submitted
 */
vector<CGJob *> *CGQueueManager::getJobsFromDb(bool start) {
    int id, i = 0;
    string name;
    string cmdlineargs;
    string token;
    string algname;
    string wuid;
    Query query = con.query();
    vector<CGJob *> *jobs = new vector<CGJob *>();
    vector<cg_job> newJobs;


    if (!start) {
        // select new jobs from db
	query << "SELECT * FROM cg_job WHERE status = \"CG_INIT\"";
	query.storein(newJobs);
    } else {
        // select old (running) jobs from db, this gets called only once at startup
	query << "SELECT * FROM cg_job WHERE status = \"CG_RUNNING\"";
	query.storein(newJobs);
    }
    
    for (vector<cg_job>::iterator it = newJobs.begin(); it != newJobs.end(); it++) {
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
	if (at == algs.end()) return jobs;
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
