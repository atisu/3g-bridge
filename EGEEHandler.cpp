#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Job.h"
#include "Logging.h"
#include "GridHandler.h"
#include "EGEEHandler.h"
#include "DBHandler.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <uuid/uuid.h>
#include <glite/lb/Job.h>
#include <glite/jdl/Ad.h>
#include <glite/jdl/adconverter.h>
#include <glite/jdl/JDLAttributes.h>
#include <glite/wms/wmproxyapi/wmproxy_api.h>
#include <glite/wmsutils/jobid/JobId.h>
#include <globus_ftp_client.h>

using namespace std;
using namespace glite::wms;
using namespace glite::jdl;
using namespace glite::wms::wmproxyapi;
using namespace glite::wmsutils::jobid;


globus_mutex_t EGEEHandler::lock;
globus_cond_t EGEEHandler::cond;
globus_bool_t EGEEHandler::done;
bool EGEEHandler::globus_err;
int EGEEHandler::global_offset;


/**
 * Constructor. Initialize ConfigContext based on passed WMProxy endpoint URL
 *
 * @param[in] jDB DB handler pointer
 * @param[in] WMProxy_EndPoint URL of the WMProxy server
 */
EGEEHandler::EGEEHandler(GKeyFile *config, const char *instance) throw (BackendException &)
{
	global_offset = 0;
	name = instance;
	wmpendp = g_key_file_get_string(config, instance, "wmproxy-endpoint", NULL);
	if (!wmpendp)
		throw BackendException("EGEE: no WMProxy endpoint for %s", instance);

	myproxy_host = g_key_file_get_string(config, instance, "myproxy_host", NULL);
	if (!myproxy_host)
		throw BackendException("EGEE: no MyProxy host for %s", instance);
	myproxy_user = g_key_file_get_string(config, instance, "myproxy_user", NULL);
	if (!myproxy_user)
		throw BackendException("EGEE: no MyProxy user for %s", instance);
	myproxy_pass = g_key_file_get_string(config, instance, "myproxy_pass", NULL);
	if (!myproxy_pass)
		throw BackendException("EGEE: no MyProxy password for %s", instance);
	myproxy_port = g_key_file_get_string(config, instance, "myproxy_port", NULL);
	if (!myproxy_port)
		myproxy_port = "7512";

	cfg = 0;

	groupByNames = false;
	maxGroupSize = 10;
	LOG(LOG_INFO, "EGEE Plugin: instance \"%s\" initialized.", instance);
}


void EGEEHandler::createCFG()
{
	renew_proxy();

	if (cfg)
		delete cfg;

	try {
    		cfg = new ConfigContext("", wmpendp, "");
	} catch (BaseException e) {
		throwStrExc(__func__, e);
		cfg = 0;
	}

	if (!cfg)
		throwStrExc(__func__, "Failed to create ConfigContext!");

	//delegate_Proxy("whatever");
}


/**
 * Destructor. Simply delete cfg.
 */
EGEEHandler::~EGEEHandler()
{
	g_free(wmpendp);
	delete cfg;
}


/**
 * Submit vector of jobs. A temporary directory is created, where every job has
 * its own directory. Input files are copied into this direcotries, and JDL
 * files are created in the "jdlfiles" directory. Next, glite-wms-job-submit is
 * executed (no documentation exists about how to use collections from the C++
 * API) to submit the JDL files as a collection. Finally, a loop tries to match
 * EGEE IDs with the received jobs.
 *
 * @param[in] jobs Pointer to the set of jobs to submit
 */
void EGEEHandler::submitJobs(JobVector &jobs) throw (BackendException &)
{
	if (!jobs.size())
		return;

	LOG(LOG_INFO, "EGEE Plugin: about to submit %zd jobs.", jobs.size());

	createCFG();
	vector<string> prodFiles;
	vector<string> prodDirs;
	char tmpl[256];
	sprintf(tmpl, "submitdir.XXXXXX");
	char *tmpdir = mkdtemp(tmpl);
	if (!tmpdir)
		throw(BackendException("Failed to create temporary directory!"));
	chdir(tmpdir);
	prodDirs.push_back(string(tmpdir));

	mkdir("jdlfiles", 0700);
	unsigned i = 0;
	for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++, i++) {
		char jdirname[32];
		sprintf(jdirname, "%d", i);
		mkdir(jdirname, 0700);
		prodDirs.push_back(string(tmpdir) + "/" + string(jdirname));
		Job *actJ = *it;

		// Get JDL template
		string jobJDLStr;
		try {
			jobJDLStr = getJobTemplate(JOBTYPE_NORMAL, actJ->getName(), "", "other.GlueCEStateStatus == \"Production\"", "- other.GlueCEStateEstimatedResponseTime", cfg);
		} catch (BaseException e) {
		    throwStrExc(__func__, e);
		}
		Ad *jobJDLAd = new Ad(jobJDLStr);

		// Copy input files, and add them to the JDL.
		// Note: files are copied, and not linket, because EGEE
		// doesn't support submitting inputsandboxes where two
		// files have the same inode.
		vector<string> ins = actJ->getInputs();
    		for (unsigned j = 0; j < ins.size(); j++) {
			string fspath = actJ->getInputPath(ins[j]).c_str();
			string oppath = ins[j];
			ifstream inf(fspath.c_str(), ios::binary);
			ofstream outf((string(jdirname) + "/" + oppath).c_str(), ios::binary);
			outf << inf.rdbuf();
			inf.close();
			outf.close();
			jobJDLAd->addAttribute(JDL::INPUTSB, (string(jdirname) + "/" + oppath).c_str());
			prodFiles.push_back(string(tmpdir) + "/" + string(jdirname) + "/" + oppath);
		}

		// Now add outputs
		vector<string> outs = actJ->getOutputs();
		for (unsigned j = 0; j < outs.size(); j++)
			jobJDLAd->addAttribute(JDL::OUTPUTSB, outs[j].c_str());

		// The arguments
		string arg = actJ->getArgs();
		if (arg.size())
			jobJDLAd->setAttribute(JDL::ARGUMENTS, arg);

		// Set some other attributes
		jobJDLAd->setAttribute(JDL::RETRYCOUNT, 10);
		jobJDLAd->setAttribute(JDL::SHALLOWRETRYCOUNT, 10);

		// Set the node name. We use this information to match the
		// EGEE IDs to jobs
		stringstream nodename;
		nodename << "Node_" << setfill('0') << setw(4) << i << "_jdl";
		jobJDLAd->setAttribute(JDL::NODE_NAME, nodename.str());

		// Now create the JDL file
		actJ->setGridId(nodename.str());
		stringstream jdlFname;
		jdlFname << "jdlfiles/" << setfill('0') << setw(4) << i << ".jdl";
		ofstream jobJDL(jdlFname.str().c_str());
		jobJDL << jobJDLAd->toString() << endl;
		jobJDL.close();
		prodFiles.push_back(string(tmpdir) + "/" + jdlFname.str());

		delete jobJDLAd;
	}

	// Submit the JDLs
	string cmd = "glite-wms-job-submit -a -o collection.id --collection jdlfiles";
	if (-1 == system(cmd.c_str()))
		throwStrExc(__func__, "Job submission using glite-wms-job-submit failed!");
	prodFiles.push_back(string(tmpdir) + "/collection.id");

	// Find out collection's ID
        ifstream collIDf("collection.id");
	string collID;
	do {
		collIDf >> collID;
	} while ("https://" != collID.substr(0, 8));

	LOG(LOG_DEBUG, "EGEE Plugin: collection subitted, now detemining node IDs.");

	// Find out job's ID
	JobId jID(collID);
	glite::lb::Job tJob(jID);
	glite::lb::JobStatus stat = tJob.status(tJob.STAT_CLASSADS|tJob.STAT_CHILDREN|tJob.STAT_CHILDSTAT);
	vector<string> childIDs = stat.getValStringList(stat.CHILDREN);
	for (unsigned i = 0; i < childIDs.size(); i++) {
		string childNodeName = "UNKNOWN";
		JobId cjID(childIDs[i]);
		glite::lb::Job ctJob(cjID);
		vector<glite::lb::Event> events;
		events.clear();
		while (!events.size()) {
			try {
				events = ctJob.log();
			} catch (...) {
				sleep(5);
			}
		}

		for (unsigned j = 0; j < events.size(); j++)
			if (events[j].type == events[j].REGJOB) {
				Ad tAd(events[j].getValString(events[j].JDL));
				childNodeName = tAd.getString(JDL::NODE_NAME);
				break;
			}

		for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
			if ((*it)->getGridId() == childNodeName) {
				(*it)->setGridId(childIDs[i]);
				(*it)->setStatus(RUNNING);
				break;
			}
	}

	chdir("..");
	cmd = "rm -rf " + string(tmpl);
	system(cmd.c_str());
	LOG(LOG_INFO, "EGEE Plugin: job submission finished.");
}


void EGEEHandler::updateStatus() throw (BackendException &)
{
	DBHandler *jobDB = DBHandler::get();
	JobVector myJobs;
	jobDB->getJobs(myJobs, getName(), RUNNING, 0);
	DBHandler::put(jobDB);

	if (myJobs.empty())
		return;

	LOG(LOG_INFO, "EGEE Plugin: about to update status of %zd jobs.", myJobs.size());

	try {
		getStatus(myJobs);
	} catch(BackendException& a) {
		throw;
	}

	LOG(LOG_INFO, "EGEE Plugin: status update finished.");
}

/*
 * Update status of jobs
 */
void EGEEHandler::getStatus(JobVector &jobs) throw (BackendException &)
{
    const struct { string EGEEs; JobStatus jobS; } statusRelation[] = {
	{"Submitted", RUNNING},
	{"Waiting", RUNNING},
	{"Ready", RUNNING},
	{"Scheduled", RUNNING},
	{"Running", RUNNING},
	{"Done", FINISHED},
	{"Cleared", ERROR},
	{"Cancelled", ERROR},
	{"Aborted", ERROR},
	{"", INIT}
    };

    if (!jobs.size())
	return;

    renew_proxy();

    for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++) {
	Job *actJ = *it;
	JobId jID(actJ->getGridId());
	glite::lb::Job tJob(jID);
	glite::lb::JobStatus stat = tJob.status(tJob.STAT_CLASSADS);
	string statStr = stat.name();
	LOG(LOG_DEBUG, "EGEE Plugin: updating status of job \"%s\".", actJ->getGridId().c_str());
	for (unsigned j = 0; statusRelation[j].EGEEs != ""; j++)
	    if (statusRelation[j].EGEEs == statStr) {
		if (FINISHED == statusRelation[j].jobS)
		    if (glite::lb::JobStatus::DONE_CODE_OK == stat.getValInt(glite::lb::JobStatus::DONE_CODE))
			getOutputs_real(actJ);
		    else
			j = 0;
		actJ->setStatus(statusRelation[j].jobS);
	    }
    }
}


/*
 * Get outputs of one job
 */
void EGEEHandler::getOutputs_real(Job *job)
{
    if (!job)
	return;

    createCFG();

    LOG(LOG_DEBUG, "EGEE Plugin: getting output of job \"%s\".", job->getGridId().c_str());
    char wd[2048];
    getcwd(wd, 2048);
    vector<pair<string, long> > URIs;
    try {
	URIs = getOutputFileList(job->getGridId(), cfg);
    } catch (BaseException e) {
	LOG(LOG_WARNING, "EGEE Plugin: failed to get output file list, I assume the job has already been fetched.");
	try {
	    cleanJob(job->getGridId());
	} catch (BackendException &e) {
	    LOG(LOG_WARNING, "EGEE Plugin: cleaning job \"%s\" failed.", job->getGridId().c_str());
	}
    }
    vector<string> remFiles(URIs.size());
    vector<string> locFiles(URIs.size());
    for (unsigned int i = 0; i < URIs.size(); i++) {
        remFiles[i] = URIs[i].first;
        string fbname = remFiles[i].substr(remFiles[i].rfind("/")+1);
        locFiles[i] = job->getOutputPath(fbname);
    }
    download_file_globus(remFiles, locFiles);
    try {
	cleanJob(job->getGridId());
    } catch (BackendException &e) {
	LOG(LOG_WARNING, "EGEE Plugin: cleaning job \"%s\" failed.", job->getGridId().c_str());
    }
}


/*
 * Cancel jobs
 */
void EGEEHandler::cancelJobs(JobVector &jobs) throw (BackendException &)
{
	if (!jobs.size())
		return;

	createCFG();

	DBHandler *jobDB = DBHandler::get();
        for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++) {
		LOG(LOG_DEBUG, "About to cancel and remove job \"" + (*it)->getId() + "\".");
		try {
			jobCancel((*it)->getGridId(), cfg);
		} catch (BaseException e) {
		}

		jobDB->deleteJob((*it)->getId());
	}
	DBHandler::put(jobDB);
}


void EGEEHandler::init_ftp_client(globus_ftp_client_handle_t *ftp_handle, globus_ftp_client_handleattr_t *ftp_handle_attrs, globus_ftp_client_operationattr_t *ftp_op_attrs)
{
    globus_result_t result;
    result = globus_ftp_client_handleattr_init(ftp_handle_attrs);
    if (GLOBUS_SUCCESS != result)
        cout << "globus_ftp_client_handleattr_init" << endl;
    result = globus_ftp_client_handle_init(ftp_handle, ftp_handle_attrs);
    if (GLOBUS_SUCCESS != result)
	cout << "globus_ftp_client_handle_init" << endl;
    result = globus_ftp_client_operationattr_init(ftp_op_attrs);
    if (GLOBUS_SUCCESS != result)
        cout << "globus_ftp_client_operationattr_init" << endl;
}


void EGEEHandler::destroy_ftp_client(globus_ftp_client_handle_t *ftp_handle, globus_ftp_client_handleattr_t *ftp_handle_attrs, globus_ftp_client_operationattr_t *ftp_op_attrs)
{
    globus_ftp_client_handleattr_destroy(ftp_handle_attrs);
    globus_ftp_client_operationattr_destroy(ftp_op_attrs);
    globus_ftp_client_handle_destroy(ftp_handle);
}


void EGEEHandler::handle_finish(void *user_args, globus_ftp_client_handle_t *ftp_handle, globus_object_t *error)
{
    if (error) {
        char *err = globus_error_print_chain(error);
	if (err)
    	    cout << "Globus handle_finish error:" << err << endl;
	else
    	    cout << "Globus handle_finish error: UNKNOWN!!!!" << endl;
        globus_err = true;
    }
    globus_mutex_lock(&lock);
    done = GLOBUS_TRUE;
    globus_cond_signal(&cond);
    globus_mutex_unlock(&lock);
    return;
}


void EGEEHandler::handle_data_write(void *user_args, globus_ftp_client_handle_t *handle, globus_object_t *error, globus_byte_t *buffer, globus_size_t buflen, globus_off_t offset, globus_bool_t eof)
{
    if (error) {
        char *err = globus_error_print_chain(error);
	if (err)
    	    cout << "Globus handle_data_write error:" << err << endl;
	else
    	    cout << "Globus handle_data_write error: UNKNOWN!!!!" << endl;
        globus_err = true;
    } else {
	if (!eof) {
	    FILE *fd = (FILE *)user_args;
	    int rc;
	    rc = fread(buffer, 1, GSIFTP_BSIZE, fd);
	    if (ferror(fd) != SUCCESS) {
		cout << "Read error in function handle_data_write; errno = " << errno << endl;
		return;
	    } 
	    globus_ftp_client_register_write(handle, buffer, rc, global_offset, feof(fd) != SUCCESS, handle_data_write, (void *)fd);
	    global_offset += rc;
	}
    }
    return;
}


void EGEEHandler::handle_data_read(void *user_args, globus_ftp_client_handle_t *ftp_handle, globus_object_t *error, globus_byte_t *buffer, globus_size_t buflen, globus_off_t offset, globus_bool_t eof)
{
    if (error) {
        char *err = globus_error_print_chain(error);
	if (err)
    	    cout << "Globus handle_data_read error:" << err << endl;
	else
    	    cout << "Globus handle_data_read error: UNKNOWN!!!!" << endl;
        globus_err = true;
    } else {
	if (buflen) {
	    FILE *fd = (FILE *)user_args;
	    fwrite(buffer, 1, buflen, fd);
	    if (ferror(fd) != SUCCESS) {
		cout << "Read error in function handle_data_read; errno = " << errno << endl;
		return;
	    } 
	    globus_ftp_client_register_read(ftp_handle, buffer, GSIFTP_BSIZE, handle_data_read, (void *)fd);
	}
    }
    return;
}


void EGEEHandler::upload_file_globus(const vector<string> &inFiles, const string &destURI)
{
    globus_byte_t *buffer;
    globus_result_t result;
    globus_ftp_client_handle_t ftp_handle;
    globus_ftp_client_handleattr_t ftp_handle_attrs;
    globus_ftp_client_operationattr_t ftp_op_attrs;

    globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    buffer = new globus_byte_t[GSIFTP_BSIZE];
    if (!buffer)
	throwStrExc(__func__, "Memory allocation error!");

    init_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);

    char *wd = new char[1024];
    wd = getcwd(wd, 1023);
    for (unsigned int i = 0; i < inFiles.size(); i++) {
	globus_err = false;
	globus_mutex_init(&lock, GLOBUS_NULL);
	globus_cond_init(&cond, GLOBUS_NULL);
	FILE *fd;
	unsigned int rbytes;
	cout << "Uploading " << inFiles[i] << " to " << destURI << "..." << endl;
	string sfile(wd);
	sfile += "/" + inFiles[i];
	string dfile(destURI);
	dfile += "/" + string(inFiles[i], inFiles[i].rfind("/")+1);
	fd = fopen(sfile.c_str(), "r");
	if (!fd)
	    throwStrExc(__func__, "Unable to open: " + sfile + "!");
	done = GLOBUS_FALSE;
	result = globus_ftp_client_put(&ftp_handle, dfile.c_str(), &ftp_op_attrs, GLOBUS_NULL, handle_finish, NULL);
	if (GLOBUS_SUCCESS != result) {
    	    char *err = globus_error_print_chain(globus_error_get(result));
	    if (err)
    		cout << "Globus globus_ftp_client_put error:" << err << endl;
	    else
    		cout << "Globus globus_ftp_client_put error: UNKNOWN!!!!" << endl;
	}
	rbytes = fread(buffer, 1, GSIFTP_BSIZE, fd);
	globus_ftp_client_register_write(&ftp_handle, buffer, rbytes, global_offset, feof(fd) != SUCCESS, handle_data_write, (void *)fd);
	global_offset += rbytes;
	globus_mutex_lock(&lock);
        while(!done)
	    globus_cond_wait(&cond, &lock);
        globus_mutex_unlock(&lock);
	fclose(fd);
	if (globus_err)
	    cout << "XXXXX Failed to upload file: " << sfile << endl;
    }
    destroy_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);
    delete[] wd;
    delete[] buffer;
}


void EGEEHandler::download_file_globus(const vector<string> &remFiles, const vector<string> &locFiles)
{
    globus_byte_t *buffer;
    globus_result_t result;
    globus_ftp_client_handle_t ftp_handle;
    globus_ftp_client_handleattr_t ftp_handle_attrs;
    globus_ftp_client_operationattr_t ftp_op_attrs;

    globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    buffer = new globus_byte_t[GSIFTP_BSIZE];
    if (!buffer)
        throwStrExc(__func__, "Memory allocation error!");

    init_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);

    for (unsigned int i = 0; i < remFiles.size(); i++) {
	FILE *outfile;
	globus_err = false;
	globus_mutex_init(&lock, GLOBUS_NULL);
	globus_cond_init(&cond, GLOBUS_NULL);
	done = GLOBUS_FALSE;
	result = globus_ftp_client_get(&ftp_handle, remFiles[i].c_str(), &ftp_op_attrs, GLOBUS_NULL, handle_finish, NULL);
	if (GLOBUS_SUCCESS != result) {
    	    char *err = globus_error_print_chain(globus_error_get(result));
	    if (err)
    		cout << "Globus globus_ftp_client_get error:" << err << endl;
	    else
    		cout << "Globus globus_ftp_client_get error: UNKNOWN!!!!" << endl;
	}
        outfile = fopen(locFiles[i].c_str(), "w");
	if (!outfile)
	    throwStrExc(__func__, "Failed to open: " + locFiles[i] + "!");
	result = globus_ftp_client_register_read(&ftp_handle, buffer, GSIFTP_BSIZE, handle_data_read, (void *)outfile);
	if (GLOBUS_SUCCESS != result)
    	    cout << "globus_ftp_client_register_read" << endl;
	globus_mutex_lock(&lock);
        while(!done)
	    globus_cond_wait(&cond, &lock);
        globus_mutex_unlock(&lock);
	fclose(outfile);
	if (globus_err)
	    cout << "XXXXX Failed to download file: " << remFiles[i] << endl;
    }
    delete[] buffer;
    destroy_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);
}


void EGEEHandler::delete_file_globus(const vector<string> &fileNames, const string &prefix)
{
    globus_result_t result;
    globus_ftp_client_handle_t ftp_handle;
    globus_ftp_client_handleattr_t ftp_handle_attrs;
    globus_ftp_client_operationattr_t ftp_op_attrs;

    globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    init_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);

    for (unsigned int i = 0; i < fileNames.size(); i++) {
	globus_err = false;
	globus_mutex_init(&lock, GLOBUS_NULL);
	globus_cond_init(&cond, GLOBUS_NULL);
	done = GLOBUS_FALSE;
	result = globus_ftp_client_delete(&ftp_handle, string(prefix + fileNames[i]).c_str(), &ftp_op_attrs, (globus_ftp_client_complete_callback_t)handle_finish, NULL);
	if (GLOBUS_SUCCESS != result) {
    	    char *err = globus_error_print_chain(globus_error_get(result));
	    if (err)
    		cout << "Globus globus_ftp_client_get error:" << err << endl;
	    else
    		cout << "Globus globus_ftp_client_get error: UNKNOWN!!!!" << endl;
	}
	globus_mutex_lock(&lock);
        while(!done)
	    globus_cond_wait(&cond, &lock);
        globus_mutex_unlock(&lock);
	if (globus_err)
	    cout << "XXXXX Failed to delete file: " << prefix + fileNames[i] << endl;
    }
    destroy_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);
}


void EGEEHandler::cleanJob(const string &jobID)
{
    int i = 0;
    LOG(LOG_DEBUG, "EGEE Plugin: cleaning job \"%s\".", jobID.c_str());
    while (i < 3) {
	try {
	    jobPurge(jobID, cfg);
	    i = 10;
	} catch (BaseException e) {
	    if (++i == 3)
		throwStrExc(__func__, e);
	}
    }
}


void EGEEHandler::delegate_Proxy(const string& delID)
{
    string proxy = grstGetProxyReq(delID, cfg);
    grstPutProxy(delID, proxy, cfg);
}


void EGEEHandler::throwStrExc(const char *func, const BaseException &e) throw (BackendException &)
{
    stringstream msg;
    msg << "Exception occured in EGEEHandler::" << func << ":" << endl;
    if (e.ErrorCode)
        msg << " Error code: " << *(e.ErrorCode) << endl;
    if (e.Description)
        msg << " Description: " << *(e.Description) << endl;
    msg << " Method name: " << e.methodName << endl;
    if (e.FaultCause)
        for (unsigned i = 0; i < (e.FaultCause)->size(); i++)
	    msg << "  FaultCause: " << (*(e.FaultCause))[i] << endl;
    throw(BackendException(msg.str()));
}


void EGEEHandler::throwStrExc(const char *func, const string &str) throw (BackendException &)
{
    stringstream msg;
    msg << "Exception occured in EGEEHandler::" << func << ": " << str;
    throw(BackendException(msg.str()));
}


void EGEEHandler::renew_proxy()
{
    string proxyf = "/tmp/proxy." + name;
    string vproxyf = "/tmp/proxy.voms." + name;
    string cmd = "echo \"" + string(myproxy_pass) + "\" | myproxy-logon -s " + string(myproxy_host) + " -p " + string(myproxy_port) + " -l " + string(myproxy_user) + " -S -o " + proxyf + " &> /dev/null";
    int rv = system(cmd.c_str());
    if (rv)
	throwStrExc(__func__, "Proxy initialization failed!");
    setenv("X509_USER_PROXY", proxyf.c_str(), 1);
    cmd = "voms-proxy-init -voms " + name + " -noregen -out " + vproxyf + " &> /dev/null";
    rv = system(cmd.c_str());
    if (-1 == rv)
	throwStrExc(__func__, "Adding VOMS extensions failed!");
    unlink(proxyf.c_str());
    setenv("X509_USER_PROXY", vproxyf.c_str(), 1);
}

GridHandler *EGEEHandler::getInstance(GKeyFile *config, const char *instance)
{
	return new EGEEHandler(config, instance);
}
