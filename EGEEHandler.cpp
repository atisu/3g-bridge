#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGJob.h"
#include "GridHandler.h"
#include "EGEEHandler.h"

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
using namespace glite::lb;
using namespace glite::wms;
using namespace glite::jdl;
using namespace glite::wms::wmproxyapi;
using namespace glite::wmsutils::jobid;


globus_mutex_t EGEEHandler::lock;
globus_cond_t EGEEHandler::cond;
globus_bool_t EGEEHandler::done;
bool EGEEHandler::globus_err;
int EGEEHandler::global_offset;


/*
 * Constructor. Initialize ConfigContext based on passed WMProxy endpoint URL
 */
EGEEHandler::EGEEHandler(JobDB *jDB, const string &WMProxy_EndPoint) throw (BackendException &)
{
    jobDB = jDB;
    global_offset = 0;
    try {
        cfg = new ConfigContext("", WMProxy_EndPoint, "");
    } catch (BaseException e) {
	throwStrExc(__func__, e);
    }
    if (!cfg)
	throwStrExc(__func__, "Failed to create ConfigContext!");
}


EGEEHandler::~EGEEHandler()
{
    delete cfg;
}


/*
 * Submit jobs
 */
void EGEEHandler::submitJobs(vector<CGJob *> *jobs) throw (BackendException &)
{
    if (!jobs || !jobs->size())
	return;

    char tmpl[256];
    sprintf(tmpl, "submitdir.XXXXXX");
    char *tmpdir = mkdtemp(tmpl);
    if (!tmpdir)
	throw(BackendException("Failed to create temporary directory!"));
    chdir(tmpdir);
    mkdir("jdlfiles", 0700);
    //CollectionAd collAd;
    vector<Ad *> jobAds;
    unsigned i = 0;
    for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++, i++) {
	char jdirname[32];
	sprintf(jdirname, "%d", i);
	mkdir(jdirname, 0700);
	CGJob *actJ = *it;
	string jobJDLStr;
	try {
	    jobJDLStr = getJobTemplate(JOBTYPE_NORMAL, actJ->getName(), "", "other.GlueCEStateStatus == \"Production\"", "- other.GlueCEStateEstimatedResponseTime", cfg);
	} catch (BaseException e) {
	    throwStrExc(__func__, e);
	}
	Ad *jobJDLAd = new Ad(jobJDLStr);
	vector<string> ins = actJ->getInputs();
        for (unsigned j = 0; j < ins.size(); j++) {
	    string fspath = actJ->getInputPath(ins[j]).c_str();
	    string oppath = ins[j];
	    ifstream inf(fspath.c_str(), ios::binary);
	    ofstream outf((string(jdirname)+"/"+oppath).c_str(), ios::binary);
	    outf << inf.rdbuf();
	    inf.close();
	    outf.close();
	    jobJDLAd->addAttribute(JDL::INPUTSB, (string(jdirname)+"/"+oppath).c_str());
	}
	vector<string> outs = actJ->getOutputs();
	for (unsigned j = 0; j < outs.size(); j++)
	    jobJDLAd->addAttribute(JDL::OUTPUTSB, outs[j].c_str());
	jobJDLAd->setAttribute(JDL::RETRYCOUNT, 10);
	jobJDLAd->setAttribute(JDL::SHALLOWRETRYCOUNT, 10);
	stringstream nodename;
	nodename << "Node_" << setfill('0') << setw(4) << i << "_jdl";
	jobJDLAd->setAttribute(JDL::NODE_NAME, nodename.str());
	actJ->setGridId(nodename.str());
	string arg = actJ->getArgs();
	if (arg.size())
	    jobJDLAd->setAttribute(JDL::ARGUMENTS, arg);
	//collAd.addNode(*jobJDLAd);
	stringstream jdlFname;
	jdlFname << "jdlfiles/" << setfill('0') << setw(4) << i << ".jdl";
	//jobAds.push_back(jobJDLAd);
	ofstream jobJDL(jdlFname.str().c_str());
	jobJDL << jobJDLAd->toString() << endl;
	jobJDL.close();
	delete jobJDLAd;
    }
    /*
    delegate_Proxy(tmpl);
    JobIdApi collID = jobRegister(collAd.toSubmissionString(), tmpl, cfg);
    cout << "The collection ID is: " << collID.jobid << endl;
    for (unsigned i = 0; i < collID.children.size(); i++)
	cout << "    Node \"" << *(collID.children[i]->nodeName) << "\" ID: " << collID.children[i]->jobid << endl;
    vector<pair<string, vector<string> > > sandboxURIs = getSandboxBulkDestURI(collID.jobid, cfg, "default");
    cout << "Queried sandbox base URIs..." << endl;
    for (unsigned i = 1; i < sandboxURIs.size(); i++) {
	vector<string> ins = jobAds[i-1]->getStringValue(JDL::INPUTSB);
	upload_file_globus(ins, sandboxURIs[i].second[0]);
    }
    jobStart(collID.jobid, cfg);
    */
    renew_proxy("seegrid");
    //string cmd = "glite-wms-job-submit -a --debug --logfile collection.log -o collection.id --collection jdlfiles";
    string cmd = "glite-wms-job-submit -a -o collection.id --collection jdlfiles";
    if (-1 == system(cmd.c_str()))
	throwStrExc(__func__, "Job submission using glite-wms-job-submit failed!");
    ifstream collIDf("collection.id");
    string collID;
    do {
	collIDf >> collID;
    } while ("https://" != collID.substr(0, 8));
    JobId jID(collID);
    glite::lb::Job tJob(jID);
    JobStatus stat = tJob.status(tJob.STAT_CLASSADS|tJob.STAT_CHILDREN|tJob.STAT_CHILDSTAT);
    vector<string> childIDs = stat.getValStringList(stat.CHILDREN);
    for (unsigned i = 0; i < childIDs.size(); i++) {
	string childNodeName = "UNKNOWN";
	JobId cjID(childIDs[i]);
	glite::lb::Job ctJob(cjID);
	vector<Event> events;
	events.clear();
	while (!events.size()) {
	    try {
		events = ctJob.log();
	    } catch (...) {
		sleep(1);
	    }
	}
	for (unsigned j = 0; j < events.size(); j++) {
	    if (events[j].type == events[j].REGJOB) {
		Ad tAd(events[j].getValString(events[j].JDL));
		childNodeName = tAd.getString(JDL::NODE_NAME);
		break;
	    }
	}
	for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++) {
	    if ((*it)->getGridId() == childNodeName) {
		//(*it)->setGridId(childIDs[i]);
		//(*it)->setStatus(CG_RUNNING);
		jobDB->updateJobGridID((*it)->getId(), childIDs[i]);
		jobDB->updateJobStat((*it)->getId(), RUNNING);
		break;
	    }
	}
    }
    chdir("..");
    rmdir(tmpdir);
}


void EGEEHandler::updateStatus() throw (BackendException &)
{
}


/*
 * Update status of jobs
 */
void EGEEHandler::getStatus(vector<CGJob *> *jobs) throw (BackendException &)
{
    const struct { string EGEEs; CGJobStatus jobS; } statusRelation[] = {
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

    if (!jobs || !jobs->size())
	return;

    renew_proxy("seegrid");

    for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++) {
	CGJob *actJ = *it;
	JobId jID(actJ->getGridId());
	glite::lb::Job tJob(jID);
	JobStatus stat = tJob.status(tJob.STAT_CLASSADS);
	string statStr = stat.name();
	for (unsigned j = 0; statusRelation[j].EGEEs != ""; j++)
	    if (statusRelation[j].EGEEs == statStr) {
		if (FINISHED == statusRelation[j].jobS)
		    getOutputs_real(actJ);
		actJ->setStatus(statusRelation[j].jobS);
		jobDB->updateJobStat(actJ->getId(), statusRelation[j].jobS);
	    }
    }
}


void EGEEHandler::getOutputs(vector<CGJob *> *jobs) throw (BackendException &)
{
}


/*
 * Get outputs of one job
 */
void EGEEHandler::getOutputs_real(CGJob *job)
{
    if (!job)
	return;

    renew_proxy("seegrid");

    char wd[2048];
    getcwd(wd, 2048);
    vector<pair<string, long> > URIs;
    try {
	URIs = getOutputFileList(job->getGridId(), cfg);
    } catch (BaseException e) {
	throwStrExc(__func__, e);
    }
    vector<string> remFiles(URIs.size());
    vector<string> locFiles(URIs.size());
    for (unsigned int i = 0; i < URIs.size(); i++) {
        remFiles[i] = URIs[i].first;
        string fbname = remFiles[i].substr(remFiles[i].rfind("/")+1);
        if (job->getOutputPath(fbname) != "")
	    locFiles[i] = job->getOutputPath(fbname);
	else {
	    locFiles[i] = string(wd) + "/" + job->getId() + "." + fbname;
	    job->setOutputPath(fbname, locFiles[i]);
	}
    }
    download_file_globus(remFiles, locFiles);
    cleanJob(job->getGridId());
}


/*
 * Cancel jobs
 */
void EGEEHandler::cancelJobs(vector<CGJob *> *jobs) throw (BackendException &)
{
    if (!jobs || !jobs->size())
	return;

    for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++) {
    }
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
    for (int i = 0; i < 3; i++) {
	try {
	    jobPurge(jobID, cfg);
	} catch (BaseException e) {
	    if (i < 2)
		continue;
	    throwStrExc(__func__, e);
	}
	return;
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


void EGEEHandler::renew_proxy(const string &voname)
{
    char proxyf[] = "/tmp/proxy.XXXXXX";
    char vproxyf[] = "/tmp/proxy.voms.XXXXXX";
    mkstemp(proxyf);
    mkstemp(vproxyf);
    string cmd = "echo \"IeKohg1A\" | myproxy-logon -s n40.hpcc.sztaki.hu -p 7512 -l bebridge -S -o " + string(proxyf) + " &> /dev/null";
    if (-1 == system(cmd.c_str()))
	throwStrExc(__func__, "Proxy initialization failed!");
    setenv("X509_USER_PROXY", proxyf, 1);
    cmd = "voms-proxy-init -voms " + voname + " -noregen -out " + string(vproxyf) + " &> /dev/null";
    if (-1 == system(cmd.c_str()))
	throwStrExc(__func__, "Adding VOMS extensions failed!");
    unlink(proxyf);
    setenv("X509_USER_PROXY", vproxyf, 1);
}
