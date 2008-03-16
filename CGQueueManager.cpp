#include "CGQueueManager.h"
#include "CGSqlStruct.h"

#include <map>
#include <list>
#include <string>
#include <iostream>
#include <sstream>
#include <uuid/uuid.h>
#include <mysql++.h>
#include <sys/stat.h>

using namespace std;
using namespace mysqlpp;

CGQueueManager::CGQueueManager(char *dcapi_conf, char *db, char *host, char *user, char *passwd)
{
    // Store base directory
    basedir = string(getcwd(NULL, 0));
    
    // Clear algorithm list
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
    string localname, inputpath;
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
	// Do not register stdout and stderr as inputs
	if (localname.compare("stdout.txt") && localname.compare("stderr.txt"))
	    if (DC_addWUOutput(wu, localname.c_str()))
		throw DC_addWUOutputError;
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

//!!!!!!!!!!!!!!! if status is running
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
	    CGJob *job = at->second;
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
		    
		}
	    } else if (job->getStatus() == CG_ERROR) {
		    // Set status of failed job to CG_ERROR
		    query << "UPDATE cg_job SET status = \"CG_ERROR\" WHERE id = " << id; 
		    query.execute();
	    }
	}
    }
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
    Query query = con.query();
    vector<CGJob *> *jobs = new vector<CGJob *>();

    // select new jobs from db, let mysql filter jobs already queued
    query << "SELECT * FROM cg_job WHERE status = \"CG_INIT\"";
    vector<cg_job> nJobs;
    query.storein(nJobs);
    
    for (vector<cg_job>::iterator it = nJobs.begin(); it != nJobs.end(); it++) {
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
	
	// Set status of job in cg_job table to running
	query.reset();
	query << "UPDATE cg_job SET status = \"CG_RUNNING\" WHERE id = " << id;
	query.execute();
    }
    return jobs;
}
