#include "CGJob.h"
#include "GridHandler.h"
#include "EGEEHandler.h"

//#include <unistd.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <glite/lb/Job.h>
#include <glite/jdl/Ad.h>
#include <glite/jdl/adconverter.h>
#include <glite/jdl/JDLAttributes.h>
//#include <glite/jdl/RequestAdExceptions.h>
//#include <glite/jdl/extractfiles.h>
#include <glite/wms/wmproxyapi/wmproxy_api.h>
#include <glite/wmsutils/jobid/JobId.h>
//#include <glite/lb/JobStatus.h>
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
EGEEHandler::EGEEHandler(const string &WMProxy_EndPoint)
{
    global_offset = 0;
    try {
        cfg = new ConfigContext("", WMProxy_EndPoint, "");
    } catch (BaseException e) {
	cout << "Exception occured during initialization of ConfigContext:" << endl;
        if (e.ErrorCode)
	    cout << e.ErrorCode << endl;
	if (e.Description)
	    cout << e.Description << endl;
    }
    if (!cfg) {
	cout << "Failed to create ConfigContext!" << endl;
	throw(-1);
    }
}


EGEEHandler::~EGEEHandler()
{
    delete cfg;
}


/*
 * Submit jobs
 */
void EGEEHandler::submitJobs(vector<CGJob *> *jobs)
{
    char tmpl[256];
    sprintf(tmpl, "submitdir.XXXXXX");
    char *tmpdir = mkdtemp(tmpl);
    if (!tmpdir) {
	cerr << "Failed to create temporary directory!" << endl;
	throw(-1);
    }
    chdir(tmpdir);
    mkdir("jdlfiles", 0700);
    //CollectionAd collAd;
    vector<Ad *> jobAds;
    for (unsigned i = 0; i < jobs->size(); i++) {
	char jdirname[32];
	sprintf(jdirname, "%d", i);
	mkdir(jdirname, 0700);
	CGJob *actJ = jobs->at(i);
	string jobJDLStr = getJobTemplate(JOBTYPE_NORMAL, actJ->getName(), "", "other.GlueCEStateStatus == \"Production\"", "- other.GlueCEStateEstimatedResponseTime", cfg);
	Ad *jobJDLAd = new Ad(jobJDLStr);
	vector<string> ins = actJ->getInputs();
        for (unsigned j = 0; j < ins.size(); j++)
	    jobJDLAd->addAttribute(JDL::INPUTSB, actJ->getInputPath(ins[j]).c_str());
	vector<string> outs = actJ->getOutputs();
	for (unsigned j = 0; j < outs.size(); j++)
	    jobJDLAd->addAttribute(JDL::OUTPUTSB, outs[j].c_str());
	jobJDLAd->addAttribute(JDL::OUTPUTSB, "stdout.log");
	jobJDLAd->setAttribute(JDL::STDOUTPUT, "stdout.log");
	jobJDLAd->addAttribute(JDL::OUTPUTSB, "stderr.log");
	jobJDLAd->setAttribute(JDL::STDERROR, "stderr.log");
	jobJDLAd->setAttribute(JDL::RETRYCOUNT, 10);
	jobJDLAd->setAttribute(JDL::SHALLOWRETRYCOUNT, 10);
	string arg;
	list<string> *args = actJ->getArgv();
	if (args) {
	    for (list<string>::iterator it = args->begin(); it != args->end(); it++)
		arg += *it + " ";
	    arg.resize(arg.length()-1);
	    jobJDLAd->setAttribute(JDL::ARGUMENTS, arg);
	}
	//collAd.addNode(*jobJDLAd);
	stringstream jdlFname;
	jdlFname << "jdlfiles/" << setfill('0') << setw(4) << i << ".jdl";
	jobAds.push_back(jobJDLAd);
	ofstream jobJDL(jdlFname.str().c_str());
	jobJDL << jobJDLAd->toString() << endl;
	jobJDL.close();
    }
    //cout << "The collection JDL is: " << collAd.toSubmissionString() << endl;
    //ofstream outColl("collection.jdl");
    //outColl << collAd.toSubmissionString() << endl;
    //outColl.close();
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
    string cmd = "glite-wms-job-submit -a -o collection.id --collection jdlfiles";
    system(cmd.c_str());
    ifstream collIDf("collection.id");
    string collID;
    do {
	collIDf >> collID;
    } while ("https://" != collID.substr(0, 8));
    cout << "Collection ID is: " << collID << endl;
    JobId jID(collID);
    glite::lb::Job tJob(jID);
    JobStatus stat = tJob.status(tJob.STAT_CLASSADS|tJob.STAT_CHILDREN|tJob.STAT_CHILDSTAT);
    vector<string> childIDs = stat.getValStringList(stat.CHILDREN);
    cout << stat.getValInt(stat.CHILDREN_NUM);
    for (unsigned int i = 0; i < childIDs.size(); i++) {
	jobs->at(i)->setGridId((char *)childIDs[i].c_str());
	jobs->at(i)->setStatus(CG_INIT);
	cout << "  New child ID: " << childIDs[i].c_str() << endl;
    }
    chdir("..");
    rmdir(tmpdir);
}


/*
 * Update status of jobs
 */
void EGEEHandler::getStatus(vector<CGJob *> *jobs)
{
    const struct { string EGEEs; CGJobStatus jobS; } statusRelation[] = {
	{"Submitted", CG_INIT},
	{"Waiting", CG_INIT},
	{"Ready", CG_INIT},
	{"Scheduled", CG_INIT},
	{"Running", CG_RUNNING},
	{"Done", CG_FINISHED},
	{"Cleared", CG_ERROR},
	{"Cancelled", CG_ERROR},
	{"Aborted", CG_ERROR},
	{"", CG_INIT}
    };
    for (unsigned i = 0; i < jobs->size(); i++) {
	JobId jID(jobs->at(i)->getGridId());
	glite::lb::Job tJob(jID);
	JobStatus stat = tJob.status(tJob.STAT_CLASSADS);
	string statStr = stat.name();
	for (unsigned j = 0; statusRelation[j].EGEEs != ""; j++)
	    if (statusRelation[j].EGEEs == statStr)
		jobs->at(i)->setStatus(statusRelation[j].jobS);
    }
}


/*
 * Get outputs of jobs
 */
void EGEEHandler::getOutputs(vector<CGJob *> *jobs)
{
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
    if (!buffer) {
        cout << "Memory allocation error!" << endl;
        exit(-1);
    }

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
	if (!fd) {
	    cout << "Unable to open: " << sfile.c_str() << endl;
	    exit(-1);
	}
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


void EGEEHandler::download_file_globus(const vector<string> &outFiles)
{
    globus_byte_t *buffer;
    globus_result_t result;
    globus_ftp_client_handle_t ftp_handle;
    globus_ftp_client_handleattr_t ftp_handle_attrs;
    globus_ftp_client_operationattr_t ftp_op_attrs;

    globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
    buffer = new globus_byte_t[GSIFTP_BSIZE];
    if (!buffer) {
        cout << "Memory allocation error!" << endl;
        exit(-1);
    }

    init_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);

    for (unsigned int i = 0; i < outFiles.size(); i++) {
	globus_err = false;
	FILE *outfile;
	char *tdfile, *dfile = strdup(outFiles[i].c_str());
	globus_mutex_init(&lock, GLOBUS_NULL);
	globus_cond_init(&cond, GLOBUS_NULL);
	cout << "Downloading " << outFiles[i] << "..." << endl;
	tdfile = basename(dfile);
	string sfile(outFiles[i]);
	done = GLOBUS_FALSE;
	result = globus_ftp_client_get(&ftp_handle, sfile.c_str(), &ftp_op_attrs, GLOBUS_NULL, handle_finish, NULL);
	if (GLOBUS_SUCCESS != result) {
    	    char *err = globus_error_print_chain(globus_error_get(result));
	    if (err)
    		cout << "Globus globus_ftp_client_get error:" << err << endl;
	    else
    		cout << "Globus globus_ftp_client_get error: UNKNOWN!!!!" << endl;
	}
        outfile = fopen(tdfile, "w");
	if (!outfile) {
	    cout << "Failed to open: " << dfile << endl;
	    exit(-1);
	}
	result = globus_ftp_client_register_read(&ftp_handle, buffer, GSIFTP_BSIZE, handle_data_read, (void *)outfile);
	if (GLOBUS_SUCCESS != result)
    	    cout << "globus_ftp_client_register_read" << endl;
	globus_mutex_lock(&lock);
        while(!done)
	    globus_cond_wait(&cond, &lock);
        globus_mutex_unlock(&lock);
	fclose(outfile);
	free(dfile);
	if (globus_err)
	    cout << "XXXXX Failed to download file: " << sfile << endl;
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
	cout << "Removing file " << prefix + fileNames[i] << endl;
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


void EGEEHandler::cleanJob(const string& jdlfile, const string &jobID)
{
}


void EGEEHandler::delegate_Proxy(const string& delID)
{
    string proxy = grstGetProxyReq(delID, cfg);
    grstPutProxy(delID, proxy, cfg);
}


#ifdef SOSEM_ER_IDE

#include "EGEE_API.h"

#include "unistd.h"
#include <iostream>
#include <vector>
#include <glite/jdl/Ad.h>
#include <glite/jdl/JDLAttributes.h>
#include <glite/jdl/extractfiles.h>
#include <glite/wms/wmproxyapi/wmproxy_api.h>
#include <glite/wmsutils/jobid/JobId.h>
#include <glite/lb/Job.h>
#include <glite/lb/JobStatus.h>
#include <glite/lb/LoggingExceptions.h>
#include <globus_ftp_client.h>

using namespace std;
using namespace glite::lb;
using namespace glite::wms;
using namespace glite::jdl;
using namespace glite::wms::wmproxyapi;
using namespace glite::wmsutils::jobid;

static globus_mutex_t lock;
static globus_cond_t cond;
static globus_bool_t done;
static bool globus_err;
int global_offset = 0;


string getStatus(const string &jobID, string *hname)
{
    string statStr = "UNKNOWN";
    try {
        JobId jID(jobID);
        Job tJob(jID);
        JobStatus stat = tJob.status(tJob.STAT_CLASSADS);
        statStr = stat.name();
        *hname = stat.getValString(stat.LOCATION);
    } catch (BaseException e) {
	cout << "Exception occured during job status query:" << endl;
        if (e.ErrorCode)
	    cout << *e.ErrorCode << endl;
	if (e.Description)
	    cout << *e.Description << endl;
    } catch (LoggingException le) {
	cout << "LoggingException thrown for job ID: " << jobID << endl;
	cout << le.dbgMessage() << endl;
	cout << le.printStackTrace() << endl;
    }
    return statStr;
}


void getOutput(const string& jdlfile, const string &jobID, ConfigContext *cfg)
{
    try {
	Ad ad;
	ad.fromFile(jdlfile);
	if (ad.hasAttribute(JDL::OUTPUTSB)) {
	    vector<pair<string, long> > URIs = getOutputFileList(jobID, cfg);
	    vector<string> inFiles(URIs.size());
	    for (unsigned int i = 0; i < URIs.size(); i++)
		inFiles[i] = URIs[i].first;
	    download_file_globus(inFiles);
	}
    } catch (BaseException e) {
        cout << "Exception occured during job output download:" << endl;
        if (e.ErrorCode)
            cout << "Error code: " << *e.ErrorCode << endl;
        if (e.Description)
    	    cout << "Description: " << *e.Description << endl;
	cout << "Method name: " << e.methodName << endl;
	if (e.FaultCause) {
	    for (unsigned int i = 0; i < (*e.FaultCause).size(); i++)
		cout << "FaultCause: " << (*e.FaultCause)[i] << endl;
	}
    }
}


void cleanJob(const string& jdlfile, const string &jobID, ConfigContext *cfg)
{
    try {
	// No need to remove sandbox files. When has this changed???
	/*
	Ad ad;
	ad.fromFile(jdlfile);

	if (ad.hasAttribute(JDL::INPUTSB)) {
	    vector<string> URIs = getSandboxDestURI(jobID, cfg, "gsiftp");
	    vector<string> inFiles = ad.getStringValue(JDL::INPUTSB);
	    URIs[0] += "/";
	    delete_file_globus(inFiles, URIs[0]);
	}
	if (ad.hasAttribute(JDL::OUTPUTSB)) {
	    vector<pair<string, long> > URIs = getOutputFileList(jobID, cfg);
	    vector<string> inFiles(URIs.size());
	    for (unsigned int i = 0; i < URIs.size(); i++)
		inFiles[i] = URIs[i].first;
	    delete_file_globus(inFiles);
	}
	*/
	jobPurge(jobID, cfg);
    } catch (BaseException e) {
        cout << "Exception occured during job purge:" << endl;
        if (e.ErrorCode)
            cout << "Error code: " << *e.ErrorCode << endl;
        if (e.Description)
    	    cout << "Description: " << *e.Description << endl;
	cout << "Method name: " << e.methodName << endl;
	if (e.FaultCause) {
	    for (unsigned int i = 0; i < (*e.FaultCause).size(); i++)
		cout << "FaultCause: " << (*e.FaultCause)[i] << endl;
	}
    }
}


#endif /* SOSEM_ER_IDE */
