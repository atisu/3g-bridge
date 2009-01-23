/*
 * Copyright (C) 2008 MTA SZTAKI LPDS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * In addition, as a special exception, MTA SZTAKI gives you permission to link the
 * code of its release of the 3G Bridge with the OpenSSL project's "OpenSSL"
 * library (or with modified versions of it that use the same license as the
 * "OpenSSL" library), and distribute the linked executables. You must obey the
 * GNU General Public License in all respects for all of the code used other than
 * "OpenSSL". If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "DBHandler.h"
#include "EGEEHandler.h"
#include "GridHandler.h"
#include "Job.h"
#include "Util.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <sys/wait.h>
#include <sys/types.h>
#include <uuid/uuid.h>
#include <glite/lb/Job.h>
#include <glite/jdl/Ad.h>
#include <glite/jdl/adconverter.h>
#include <glite/jdl/JDLAttributes.h>
#include <glite/wms/wmproxyapi/wmproxy_api.h>
#include <glite/wmsutils/jobid/JobId.h>
#include <glite/wmsutils/exception/Exception.h>
#include <globus_ftp_client.h>
#include <globus_gsi_credential.h>


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
static const char *tmppath;


/**
 * Invoke a command. Imported from DCAPIHandler and slightly modified
 * so stdout and stderr is returned to the caller.
 */
static int invoke_cmd(const char *exe, const char *const argv[], string *stdouts, string *stderrs) throw (BackendException *)
{
        int socks[2][2];

        for (int i = 0; i < 2; i++)
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, socks[0]) == -1 ||
			socketpair(AF_UNIX, SOCK_STREAM, 0, socks[1]) == -1)
                        throw new BackendException("socketpair() failed: %s",
				strerror(errno));

        pid_t pid = fork();
        if (pid)
        {
                /* Parent */
                int status;
            	char buf[1024];
		string *strs[2] = {stdouts, stderrs};

		for (int i = 0; i < 2; i++)
		{
            		close(socks[i][0]);
			if (strs[i])
			{
				*strs[i] = "";
            			while ((status = read(socks[i][1], buf, sizeof(buf))) > 0)
                    			strs[i]->append(buf, status);
			}
            		close(socks[i][1]);
		}

                if (waitpid(pid, &status, 0) == -1)
                        throw new BackendException("waitpid() failed: %s",
				strerror(errno));
                if (WIFSIGNALED(status))
                        throw new BackendException("Command %s died with signal %d",
                		exe, WTERMSIG(status));

                return WEXITSTATUS(status);
        }

        /* Child */
        int fd = open("/dev/null", O_RDWR);
        dup2(fd, 0);
        close(fd);

        dup2(socks[0][0], 1);
        dup2(socks[1][0], 2);

        for (int i = 0; i < 2; i++)
                for (int j = 0; j < 2; j++)
                	close(socks[i][j]);

        execvp(exe, (char *const *)argv);
        _Exit(255);
}


EGEEHandler::EGEEHandler(GKeyFile *config, const char *instance) throw (BackendException *)
{
	char buf[128];

	global_offset = 0;
	name = instance;

	wmpendp = g_key_file_get_string(config, instance, "wmproxy-endpoint", NULL);
	if (!wmpendp)
		throw new BackendException("EGEE: no WMProxy endpoint for %s", instance);
	voname = g_key_file_get_string(config, instance, "voname", NULL);
	if (!voname)
		throw new BackendException("EGEE: no Virtual Organization for %s", instance);

	myproxy_host = g_key_file_get_string(config, instance, "myproxy_host", NULL);
	if (!myproxy_host)
		throw new BackendException("EGEE: no MyProxy host for %s", instance);
	myproxy_user = g_key_file_get_string(config, instance, "myproxy_user", NULL);
	if (!myproxy_user)
		throw new BackendException("EGEE: no MyProxy user for %s", instance);
	myproxy_authcert = g_key_file_get_string(config, instance, "myproxy_authcert", NULL);
	if (!myproxy_authcert)
		throw new BackendException("EGEE: no MyProxy authcert for %s", instance);
	myproxy_authkey = g_key_file_get_string(config, instance, "myproxy_authkey", NULL);
	if (!myproxy_authkey)
		throw new BackendException("EGEE: no MyProxy authkey for %s", instance);
	myproxy_port = g_key_file_get_string(config, instance, "myproxy_port", NULL);
	if (!myproxy_port)
		myproxy_port = "7512";

	cfg = 0;

	if (!tmppath) {
		char *tmp = getenv("TMPDIR");
		tmppath = (tmp ? tmp : "/tmp");
	}
		
	snprintf(buf, sizeof(buf), "%s/.egee_%s_XXXXXX", tmppath, instance);
	if (!mkdtemp(buf))
		throw new BackendException("EGEE: failed to create temp. directory \"%s\"", buf);
	tmpdir = buf;

	groupByNames = false;
	LOG(LOG_INFO, "EGEE Plugin: instance \"%s\" initialized.", instance);
}


void EGEEHandler::createCFG() throw(BackendException *)
{
	renew_proxy();

	if (cfg)
		delete cfg;

	try
	{
    		cfg = new ConfigContext("", wmpendp, "");
	}
	catch (BaseException &e)
	{
		throw new BackendException(getEGEEErrMsg(e));
	}

	if (!cfg)
		throw new BackendException("Failed to create ConfigContext for unknown reason");
}


EGEEHandler::~EGEEHandler()
{
	const char *rmargs[] = { "rm", "-rf", tmpdir.c_str(), NULL };

	invoke_cmd("rm", rmargs, NULL, NULL);
	g_free(voname);
	g_free(wmpendp);
	g_free(myproxy_host);
	g_free(myproxy_user);
	g_free(myproxy_authcert);
	g_free(myproxy_authkey);
	g_free(myproxy_port);
	delete cfg;

}


void EGEEHandler::submitJobs(JobVector &jobs) throw (BackendException *)
{
	char wdir[PATH_MAX];
	char jdldir[PATH_MAX];
	char jobdir[PATH_MAX];

	if (!jobs.size())
		return;

	LOG(LOG_INFO, "EGEE Plugin (%s): about to submit %zd jobs.", name.c_str(), jobs.size());

	try
	{
		createCFG();
	}
	catch (BackendException *e)
	{
		LOG(LOG_ERR, "EGEE Plugin (%s): failed to create ConfigContext (%s), so %zd jobs are marked as failed.",
			name.c_str(), e->what(), jobs.size());
		delete e;
		for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
			(*it)->setStatus(Job::ERROR);
		return;
	}
	snprintf(wdir, sizeof(wdir), "%s/submitdir.XXXXXX", tmpdir.c_str());
	char *ttmpdir = mkdtemp(wdir);
	if (!ttmpdir)
	{
		LOG(LOG_ERR, "EGEE Plugin (%s): failed to create temporary directory for submission",
			name.c_str());
		return;
	}

	sprintf(jdldir, "%s/jdlfiles", wdir);
	sprintf(jobdir, "%s/jobfiles", wdir);
	mkdir(jdldir, 0700);
	mkdir(jobdir, 0700);
	unsigned i = 0;
	for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
	{
		char jdirname[PATH_MAX];
		sprintf(jdirname, "%s/%d", jobdir, i);
		mkdir(jdirname, 0700);
		Job *actJ = *it;

		string jobJDLStr;
		try
		{
			jobJDLStr = getJobTemplate(JOBTYPE_NORMAL, actJ->getName(), "",
				"other.GlueCEStateStatus == \"Production\"",
				"- other.GlueCEStateEstimatedResponseTime", cfg);
		}
		catch (BaseException &e)
		{
			LOG(LOG_WARNING, "EGEE Plugin (%s): failed to get job JDL template, EGEE exception follows:",
				name.c_str());
			LOG(LOG_WARNING, getEGEEErrMsg(e).c_str());
			LOG(LOG_WARNING, "EGEE Plugin (%s): marking job %s as "
				"failed.", name.c_str(), (*it)->getId().c_str());
			(*it)->setStatus(Job::ERROR);
			continue;
		}

		Ad *jobJDLAd = new Ad(jobJDLStr);

		// Copy input files, and add them to the JDL.
		// Note: files are copied, and not linket, because EGEE
		// doesn't support submitting inputsandboxes where two
		// files have the same inode.
		auto_ptr< vector<string> > ins = actJ->getInputs();
    		for (unsigned j = 0; j < ins->size(); j++) {
			string fspath = actJ->getInputPath((*ins)[j]).c_str();
			string oppath = (*ins)[j];
			ifstream inf(fspath.c_str(), ios::binary);
			ofstream outf((string(jdirname) + "/" + oppath).c_str(), ios::binary);
			outf << inf.rdbuf();
			inf.close();
			outf.close();
			jobJDLAd->addAttribute(JDL::INPUTSB, (string(jdirname) + "/" + oppath).c_str());
		}

		// Now add outputs
		auto_ptr<vector<string> > outs = actJ->getOutputs();
		for (unsigned j = 0; j < outs->size(); j++)
			jobJDLAd->addAttribute(JDL::OUTPUTSB, (*outs)[j].c_str());

		// The arguments
		string arg = actJ->getArgs();
		if (arg.size())
		{
			gchar *sargs = g_strdup(arg.c_str());
			gchar *pargs = sargs;
			while (*pargs != '\0')
			{
				if (*pargs == '"')
					*pargs = '\'';
				pargs++;
			}
			jobJDLAd->setAttribute(JDL::ARGUMENTS, sargs);
			g_free(sargs);
		}

		// Set some other attributes
		jobJDLAd->setAttribute(JDL::RETRYCOUNT, 10);
		jobJDLAd->setAttribute(JDL::SHALLOWRETRYCOUNT, 10);
		jobJDLAd->setAttribute(JDL::MYPROXY, myproxy_host);

		// Set the node name. We use this information to match the
		// EGEE IDs to jobs
		stringstream nodename;
		nodename << "Node_" << setfill('0') << setw(4) << i << "_jdl";
		jobJDLAd->setAttribute(JDL::NODE_NAME, nodename.str());

		// Now create the JDL file
		actJ->setGridId(nodename.str());
		stringstream jdlFname;
		jdlFname << jdldir << "/" << setfill('0') << setw(4) << i << ".jdl";
		ofstream jobJDL(jdlFname.str().c_str());
		jobJDL << jobJDLAd->toString() << endl;
		jobJDL.close();
		LOG(LOG_DEBUG, "JDL is: %s", jobJDLAd->toString().c_str());

		delete jobJDLAd;
		i++;
	}

	if (!i)
	{
		LOG(LOG_INFO, "EGEE Plugin (%s): due to the previous errors, there are no jobs to submit.", name.c_str());
		const char *rmargs[] = { "rm", "-rf", wdir, NULL };
		invoke_cmd("rm", rmargs, NULL, NULL);
		return;
	}

	// Submit the JDLs
	string sout, serr;
	string collidfile = string(wdir) + "/collection.id";
	try
	{
		const char *submitargs[] = {"glite-wms-job-submit", "-a", "-e",
			wmpendp, "-o", collidfile.c_str(), "--collection", jdldir, NULL};
		if (invoke_cmd("glite-wms-job-submit", submitargs, &sout, &serr))
		{
			LOG(LOG_ERR, "EGEE Plugin (%s): job submission failed:"
				"\n%s\n%s", name.c_str(), sout.c_str(),
				serr.c_str());
			const char *rmargs[] = { "rm", "-rf", wdir, NULL };
			invoke_cmd("rm", rmargs, NULL, NULL);
			return;
		}
	}
	catch (BackendException *e)
	{
		LOG(LOG_ERR, "EGEE Plugin (%s): job submission failed: %s",
			name.c_str(), e->what());
		const char *rmargs[] = { "rm", "-rf", wdir, NULL };
		invoke_cmd("rm", rmargs, NULL, NULL);
		return;
	}
	

	// Find out collection's ID
        ifstream collIDf(collidfile.c_str());
	string collID;
	do
	{
		collIDf >> collID;
	} while ("https://" != collID.substr(0, 8));

	const char *rmargs[] = { "rm", "-rf", wdir, NULL };
	invoke_cmd("rm", rmargs, NULL, NULL);

	LOG(LOG_DEBUG, "EGEE Plugin (%s): collection subitted, now detemining node IDs.", name.c_str());

	// Find out job's ID
	glite::lb::JobStatus stat;
	try
	{
		JobId jID(collID);
		glite::lb::Job tJob(jID);
		tJob.setParam(EDG_WLL_PARAM_X509_PROXY, tmpdir + "/proxy.voms");
		stat = tJob.status(tJob.STAT_CLASSADS|tJob.STAT_CHILDREN|tJob.STAT_CHILDSTAT);
	}
	catch (BaseException &e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to get child nodes of collection \"%s\", EGEE exception follows:",
			name.c_str(), collID.c_str());
		LOG(LOG_WARNING, getEGEEErrMsg(e).c_str());
		return;
	}
	catch (glite::wmsutils::exception::Exception &e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to get child nodes of collection \"%s\", EGEE exception follows:",
			name.c_str(), collID.c_str());
		LOG(LOG_WARNING, e.dbgMessage().c_str());
		return;
	}
	vector<string> childIDs = stat.getValStringList(stat.CHILDREN);
	for (unsigned i = 0; i < childIDs.size(); i++) {
		string childNodeName = "UNKNOWN";
		JobId cjID(childIDs[i]);
		glite::lb::Job ctJob(cjID);
		ctJob.setParam(EDG_WLL_PARAM_X509_PROXY, tmpdir + "/proxy.voms");
		vector<glite::lb::Event> events;
		events.clear();
		int tries = 0;
		while (!events.size() && tries < 3) {
			try {
				LOG(LOG_DEBUG, "EGEE Plugin (%s): trying to determine node IDs", name.c_str());
				events = ctJob.log();
				tries++;
			} catch (...) {
				sleep(20);
				tries++;
			}
		}
		if (tries == 3 && !events.size())
		{
			LOG(LOG_WARNING, "EGEE Plugin (%s): failed to detemine node IDs for 60 seconds, skipping submission...", name.c_str());
			break;
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
				(*it)->setStatus(Job::RUNNING);
				LOG(LOG_INFO, "EGEE Plugin (%s): submitted job \"%s\" (grid identifier is \"%s\").", name.c_str(), (*it)->getId().c_str(), (*it)->getGridId().c_str());
				break;
			}
	}
	LOG(LOG_INFO, "EGEE Plugin (%s): job submission finished.", name.c_str());
}


void EGEEHandler::updateStatus(void) throw (BackendException *)
{
	LOG(LOG_DEBUG, "EGEE Plugin (%s): about to update status of jobs.", name.c_str());

	try
	{
		createCFG();
	}
	catch (BackendException *e)
	{
		LOG(LOG_ERR, "EGEE Plugin (%s): failed to create ConfigContext (%s), skipping job status update.",
			name.c_str(), e->what());
		delete e;
		return;
	}

	DBHandler *jobDB = DBHandler::get();
	jobDB->pollJobs(this, Job::RUNNING, Job::CANCEL);
	DBHandler::put(jobDB);

	LOG(LOG_DEBUG, "EGEE Plugin (%s): status update finished.", name.c_str());
}


void EGEEHandler::updateJob(Job *job)
{
	const struct { string EGEEs; Job::JobStatus jobS; } statusRelation[] = {
		{"Submitted", Job::RUNNING},
		{"Waiting", Job::RUNNING},
		{"Ready", Job::RUNNING},
		{"Scheduled", Job::RUNNING},
		{"Running", Job::RUNNING},
		{"Done", Job::FINISHED},
		{"Cleared", Job::ERROR},
		{"Cancelled", Job::ERROR},
		{"Aborted", Job::ERROR},
		{"", Job::INIT}
        };

	int tries = 0;
	string statStr = "";
	int statCode = 0;
	while (tries < 3)
	{
		try
		{
			JobId jID(job->getGridId());
			glite::lb::Job tJob(jID);
			tJob.setParam(EDG_WLL_PARAM_X509_PROXY, tmpdir + "/proxy.voms");
		        glite::lb::JobStatus stat = tJob.status(tJob.STAT_CLASSADS);
	    		statStr = stat.name();
			statCode = stat.getValInt(glite::lb::JobStatus::DONE_CODE);
			tries = 3;
		}
		catch (BaseException &e)
		{
			LOG(LOG_WARNING, "EGEE Plugin (%s): failed to update status of job \"%s\" (attempt %d), EGEE exception follows:",
				name.c_str(), job->getGridId().c_str(), ++tries);
			LOG(LOG_WARNING, getEGEEErrMsg(e).c_str());
			if (tries >= 3)
			{
				job->setStatus(Job::ERROR);
				cleanJob(job->getGridId());
				return;
			}
			sleep(5);
		}
		catch (glite::wmsutils::exception::Exception &e)
		{
			LOG(LOG_WARNING, "EGEE Plugin (%s): failed to update status of job \"%s\" (attempt %d), EGEE exception follows:",
				name.c_str(), job->getGridId().c_str(), ++tries);
			LOG(LOG_WARNING, e.dbgMessage().c_str());
			if (tries >= 3)
			{
				job->setStatus(Job::ERROR);
				cleanJob(job->getGridId());
				return;
			}
			sleep(5);
		}
	}

	LOG(LOG_DEBUG, "EGEE Plugin (%s): updating status of job \"%s\" (unique identifier is \"%s\", EGEE status is \"%s\").",
		name.c_str(), job->getGridId().c_str(), job->getId().c_str(), statStr.c_str());
	for (unsigned j = 0; statusRelation[j].EGEEs != ""; j++)
	{
		if (statusRelation[j].EGEEs == statStr)
		{
			if (Job::FINISHED == statusRelation[j].jobS)
			{
				if (glite::lb::JobStatus::DONE_CODE_OK == statCode)
					getOutputs_real(job);
				else
					break;
			}
			job->setStatus(statusRelation[j].jobS);
			if (Job::ERROR == statusRelation[j].jobS)
				cleanJob(job->getGridId());
		}
	}
}


void EGEEHandler::cancelJob(Job *job)
{
	LOG(LOG_DEBUG, "About to cancel and remove job \"%s\".", job->getId().c_str());
	try
	{
		jobCancel(job->getGridId(), cfg);
	}
	catch (BaseException &e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to cancel job \"%s\", EGEE exception follows:",
			name.c_str(), job->getGridId().c_str());
		LOG(LOG_WARNING, getEGEEErrMsg(e).c_str());
	}

	DBHandler *jobDB = DBHandler::get();
	jobDB->deleteJob(job->getId());
	DBHandler::put(jobDB);
}


void EGEEHandler::poll(Job *job) throw (BackendException *)
{
	switch (job->getStatus())
	{
		case Job::RUNNING:
			updateJob(job);
			break;
		case Job::CANCEL:
			cancelJob(job);
			break;
		default:
			break;
	}
}


void EGEEHandler::getOutputs_real(Job *job)
{
	if (!job)
		return;

	try
	{
		createCFG();
	}
	catch (BackendException *e)
	{
		LOG(LOG_ERR, "EGEE Plugin (%s): failed to create ConfigContext (%s), skipping job result download.",
			name.c_str(), e->what());
		delete e;
		return;
	}

	LOG(LOG_INFO, "EGEE Plugin (%s): getting output of job \"%s\".",
		name.c_str(), job->getGridId().c_str());

	vector<pair<string, long> > URIs;
	try
	{
		URIs = getOutputFileList(job->getGridId(), cfg);
    		vector<string> remFiles(URIs.size());
	        vector<string> locFiles(URIs.size());
	        for (unsigned int i = 0; i < URIs.size(); i++)
		{
    			remFiles[i] = URIs[i].first;
    			string fbname = remFiles[i].substr(remFiles[i].rfind("/")+1);
	    		locFiles[i] = job->getOutputPath(fbname);
		}
    		download_file_globus(remFiles, locFiles);
    		delete_file_globus(remFiles, "");
		cleanJob(job->getGridId());
	}
	catch (BaseException &e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to get output file list, I assume the job has already been fetched, but EGEE exception is here for your convenience: ",
			name.c_str());
		LOG(LOG_WARNING, getEGEEErrMsg(e).c_str());
	}
	catch (BackendException *e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): cleaning job \"%s\" failed.",
			name.c_str(), job->getGridId().c_str());
		delete e;
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
	int i = 0;
	LOG(LOG_INFO, "EGEE Plugin (%s): cleaning job \"%s\".", name.c_str(), jobID.c_str());
	while (i < 3)
	{
		try
		{
			jobPurge(jobID, cfg);
			i = 10;
		}
		catch (BaseException &e)
		{
			i++;
			LOG(LOG_WARNING, "EGEE Plugin (%s): job clean attempt %d failed, EGEE exception follows:", name.c_str(), i);
			LOG(LOG_WARNING, getEGEEErrMsg(e).c_str());
		}
	}
}


void EGEEHandler::delegate_Proxy(const string& delID)
{
    string proxy = grstGetProxyReq(delID, cfg);
    grstPutProxy(delID, proxy, cfg);
}


string EGEEHandler::getEGEEErrMsg(const BaseException &e)
{
    stringstream msg;
    msg << "EGEE exception caught:" << endl;
    if (e.ErrorCode)
        msg << "  Error code: " << *(e.ErrorCode) << endl;
    if (e.Description)
        msg << "  Description: " << *(e.Description) << endl;
    msg << "  Method name: " << e.methodName << endl;
    if (e.FaultCause)
        for (unsigned i = 0; i < (e.FaultCause)->size(); i++)
	    msg << "   FaultCause: " << (*(e.FaultCause))[i] << endl;
    return msg.str();
}


void EGEEHandler::throwStrExc(const char *func, const BaseException &e) throw (BackendException *)
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
    throw new BackendException(msg.str());
}


void EGEEHandler::throwStrExc(const char *func, const string &str) throw (BackendException *)
{
    stringstream msg;
    msg << "Exception occured in EGEEHandler::" << func << ": " << str;
    throw new BackendException(msg.str());
}


void EGEEHandler::renew_proxy() throw(BackendException *)
{
	time_t lifetime;
	string proxyf = tmpdir + "/proxy";
	string vproxyf = tmpdir + "/proxy.voms";

	getProxyInfo(vproxyf.c_str(), &lifetime);
	// Do not update proxy if it is valid for at least 6 hrs
	if (lifetime > 6*60*60) {
		setenv("X509_USER_PROXY", vproxyf.c_str(), 1);
		return;
	}

	LOG(LOG_DEBUG, "EGEE Plugin (%s): about to renew proxy.",
		name.c_str());
	string cmd = "X509_USER_CERT='" + string(myproxy_authcert) + "' X509_USER_KEY='" + string(myproxy_authkey)
		+ "' myproxy-logon -s '" + string(myproxy_host) + "' -p '" + string(myproxy_port)
		+ "' -l '" + string(myproxy_user) + "' -n -t 24 -o '" + proxyf + "' >/dev/null 2>&1";
	int rv = system(cmd.c_str());
	if (rv)
		throw new BackendException("Proxy initialization failed");

	cmd = "X509_USER_PROXY='" + proxyf + "' voms-proxy-init -voms '" + string(voname)
		+ "' -noregen -out '" + vproxyf + "' -valid 23:00 >/dev/null 2>&1";
	rv = system(cmd.c_str());
	if (-1 == rv)
		throw new BackendException("Adding VOMS extensions to proxy failed!");

	unlink(proxyf.c_str());
	setenv("X509_USER_PROXY", vproxyf.c_str(), 1);
	LOG(LOG_DEBUG, "EGEE Plugin (%s): proxy renewal finished.", name.c_str());
}


GridHandler *EGEEHandler::getInstance(GKeyFile *config, const char *instance)
{
	return new EGEEHandler(config, instance);
}


char *EGEEHandler::getProxyInfo(const char *proxyfile, time_t *lifetime)
{
	char *id;
	*lifetime = 0;

	if (!proxyfile)
		return NULL;

        globus_module_activate(GLOBUS_GSI_CREDENTIAL_MODULE);

        globus_gsi_cred_handle_t handle;
        globus_gsi_cred_handle_attrs_t attrs;
        if (GLOBUS_SUCCESS != globus_gsi_cred_handle_attrs_init(&attrs))
		return NULL;
        if (GLOBUS_SUCCESS != globus_gsi_cred_handle_init(&handle, attrs)) {
		globus_gsi_cred_handle_attrs_destroy(attrs);
		return NULL;
	}

        if (GLOBUS_SUCCESS != globus_gsi_cred_read_proxy(handle, proxyfile)) {
		globus_gsi_cred_handle_destroy(handle);
		globus_gsi_cred_handle_attrs_destroy(attrs);
                return NULL;
	}

        if (GLOBUS_SUCCESS != globus_gsi_cred_get_identity_name(handle, &id)) {
		globus_gsi_cred_handle_destroy(handle);
		globus_gsi_cred_handle_attrs_destroy(attrs);
		return NULL;
	}

	if (GLOBUS_SUCCESS != globus_gsi_cred_get_lifetime(handle, lifetime)) {
		globus_gsi_cred_handle_destroy(handle);
		globus_gsi_cred_handle_attrs_destroy(attrs);
		return NULL;
	}

	globus_gsi_cred_handle_destroy(handle);
	globus_gsi_cred_handle_attrs_destroy(attrs);

	return id;
}
