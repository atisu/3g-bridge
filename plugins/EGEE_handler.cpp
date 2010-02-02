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
#include "EGEE_handler.h"
#include "GridHandler.h"
#include "Job.h"
#include "Util.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <errno.h>
#include <string.h>
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
#include <globus_gass_copy.h>
#include <globus_ftp_client.h>
#include <globus_gsi_credential.h>

extern "C" {
#include <myproxy.h>
}

using namespace std;
using namespace glite::wms;
using namespace glite::jdl;
using namespace glite::wms::wmproxyapi;
using namespace glite::wmsutils::jobid;


globus_mutex_t EGEEHandler::lock;
globus_cond_t EGEEHandler::cond;
globus_bool_t EGEEHandler::done;
bool EGEEHandler::globus_err;
char *EGEEHandler::globus_errmsg;
static const char *tmppath;


typedef enum {NONE, ERROR, ALL} llevel;

/**
 * Invoke a command. Imported from DCAPIHandler and slightly modified
 * so stdout and stderr is returned to the caller.
 */
static int invoke_cmd(const char *exe, const char *const argv[], string *stdoe) throw (BackendException *)
{
	int sock[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock) == -1)
		throw new BackendException("socketpair() failed: %s",
			strerror(errno));

	pid_t pid = fork();
	if (pid)
	{
		/* Parent */
		int status;
		char buf[1024];

		close(sock[0]);
		if (stdoe)
		{
			*stdoe = "";
			while ((status = read(sock[1], buf, sizeof(buf))) > 0)
				stdoe->append(buf, status);
		}
		close(sock[1]);

		if (waitpid(pid, &status, 0) == -1)
			throw new BackendException("waitpid() failed: %s",
				strerror(errno));
		if (WIFSIGNALED(status))
			throw new BackendException("Command %s died with "
				"signal %d", exe, WTERMSIG(status));

		return WEXITSTATUS(status);
	}

	/* Child */
	int fd = open("/dev/null", O_RDWR);
	dup2(fd, 0);
	close(fd);

	dup2(sock[0], 1);
	dup2(sock[0], 2);
	close(sock[0]);
	close(sock[1]);

	execvp(exe, (char *const *)argv);
	_Exit(255);
}


//
// Public methods
//


EGEEHandler::EGEEHandler(GKeyFile *config, const char *instance) throw (BackendException *): GridHandler(config, instance)
{
	char buf[128];

	wmpendp = g_key_file_get_string(config, instance, "wmproxy-endpoint",
		NULL);
	if (!wmpendp)
		throw new BackendException("EGEE: no WMProxy endpoint for %s",
			instance);
	voname = g_key_file_get_string(config, instance, "voname", NULL);
	if (!voname)
		throw new BackendException("EGEE: no Virtual Organization for "
			"%s", instance);

	myproxy_host = g_key_file_get_string(config, instance, "myproxy_host",
		NULL);
	if (!myproxy_host)
		throw new BackendException("EGEE: no MyProxy host for %s",
			instance);
	myproxy_user = g_key_file_get_string(config, instance, "myproxy_user",
		NULL);
	if (!myproxy_user)
		throw new BackendException("EGEE: no MyProxy user for %s",
			instance);
	myproxy_authcert = g_key_file_get_string(config, instance,
		"myproxy_authcert", NULL);
	if (!myproxy_authcert)
		throw new BackendException("EGEE: no MyProxy authcert for %s",
			instance);
	myproxy_authkey = g_key_file_get_string(config, instance,
		"myproxy_authkey", NULL);
	if (!myproxy_authkey)
		throw new BackendException("EGEE: no MyProxy authkey for %s",
			instance);
	myproxy_port = g_key_file_get_string(config, instance, "myproxy_port",
		NULL);
	if (!myproxy_port)
		myproxy_port = "7512";
	isb_url = g_key_file_get_string(config, instance, "isb_url", NULL);
	if (!isb_url)
		throw new BackendException("EGEE: no inputsandbox URL specified"
			" for %s", instance);

	globus_errmsg = NULL;

	cfg = 0;

	if (!tmppath) {
		char *tmp = getenv("TMPDIR");
		tmppath = (tmp ? tmp : "/tmp");
	}

	snprintf(buf, sizeof(buf), "%s/.egee_%s_XXXXXX", tmppath, instance);
	if (!mkdtemp(buf))
		throw new BackendException("EGEE: failed to create temporary "
			"directory \"%s\"", buf);
	tmpdir = buf;

	jobloglevel = NONE;
	joblogdir = g_key_file_get_string(config, instance, "joblogdir", NULL);
	if (joblogdir && strlen(joblogdir))
	{
		char *jl = g_key_file_get_string(config, instance, "joblogs",
			NULL);
		if (!strcmp(jl, "error"))
			jobloglevel = ERROR;
		else if (!strcmp(jl, "all"))
			jobloglevel = ALL;
		free(jl);
	}

	groupByNames = false;
	LOG(LOG_INFO, "EGEE Plugin: instance \"%s\" initialized.", instance);
}


EGEEHandler::~EGEEHandler()
{
	const char *rmargs[] = { "rm", "-rf", tmpdir.c_str(), NULL };

	invoke_cmd("rm", rmargs, NULL);
	g_free(voname);
	g_free(wmpendp);
	g_free(myproxy_host);
	g_free(myproxy_user);
	g_free(myproxy_authcert);
	g_free(myproxy_authkey);
	g_free(myproxy_port);
	g_free(isb_url);
	delete cfg;
}


void EGEEHandler::submitJobs(JobVector &jobs) throw (BackendException *)
{
	char wdir[PATH_MAX];
	char jdldir[PATH_MAX];

	if (!jobs.size())
		return;

	LOG(LOG_INFO, "EGEE Plugin (%s): about to submit %zd job(s).",
		name.c_str(), jobs.size());

	try
	{
		createCFG();
	}
	catch (BackendException *e)
	{
		LOG(LOG_ERR, "EGEE Plugin (%s): failed to create ConfigContext "
			"(%s), so %zd jobs are marked as temporary failed.",
			name.c_str(), e->what(), jobs.size());
		delete e;
		for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
			(*it)->setStatus(Job::TEMPFAILED);
		return;
	}

	snprintf(wdir, sizeof(wdir), "%s/submitdir.XXXXXX", tmpdir.c_str());
	char *ttmpdir = mkdtemp(wdir);
	if (!ttmpdir)
	{
		LOG(LOG_ERR, "EGEE Plugin (%s): failed to create temporary "
			"directory for submission", name.c_str());
		return;
	}

	const char *rmargs[] = { "rm", "-rf", wdir, NULL };
	sprintf(jdldir, "%s/jdlfiles", wdir);
	mkdir(jdldir, 0700);
	unsigned i = 0;

	string jobJDLStr;
	try
	{
		jobJDLStr = getJobTemplate(JOBTYPE_NORMAL, "wrapscript.sh", "",
			"other.GlueCEStateStatus == \"Production\"",
			"- other.GlueCEStateEstimatedResponseTime", cfg);
	}
	catch (BaseException &e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to get job JDL "
			"template, EGEE exception follows:\n%s", name.c_str(),
			getEGEEErrMsg(e).c_str());
		LOG(LOG_WARNING, "EGEE Plugin (%s): marking jobs as temporary "
			"failed.", name.c_str());
		for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
			(*it)->setStatus(Job::TEMPFAILED);
		return;
	}

	for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
	{
		Job *actJ = *it;
		string jid = actJ->getId();
		const char *cjid = jid.c_str();

		auto_ptr<vector<string> > ins  = actJ->getInputs();
		auto_ptr<vector<string> > outs = actJ->getOutputs();

		string jobprefix = string(isb_url) + "/" + jid + "/";

		try
		{
			LOG(LOG_DEBUG, "EGEE Plugin (%s): about to create "
				"remote working directory \"%s\" for job \"%s\"",
				name.c_str(), jobprefix.c_str(), cjid);
			create_dir_globus(jobprefix);
		}
		catch (BackendException *e)
		{
			LOG(LOG_ERR, "EGEE Plugin (%s): failed to create remote"
				" working direcotry of job \"%s\": %s",
				name.c_str(), cjid, e->what());
			actJ->setStatus(Job::TEMPFAILED);
			delete e;
			continue;
		}

		Ad *jobJDLAd = new Ad(jobJDLStr);
		jobJDLAd->delAttribute(JDL::EXECUTABLE);
		jobJDLAd->setAttribute(JDL::EXECUTABLE, actJ->getName());

		// Copy input files to inputsandbox url
		LOG(LOG_DEBUG, "EGEE Plugin (%s): about to upload input files "
			"belonging to job \"%s\"", name.c_str(), cjid);
		vector<string> srcFiles;
		vector<string> dstFiles;
		for (unsigned j = 0; j < ins->size(); j++)
		{
			string ipath = actJ->getInputPath((*ins)[j]);
			string rpath = jobprefix + (*ins)[j];
			srcFiles.push_back("file://" + ipath);
			dstFiles.push_back(rpath);
			jobJDLAd->addAttribute(JDL::INPUTSB, rpath.c_str());
		}

		try
		{
			transfer_files_globus(srcFiles, dstFiles);
		}
		catch (BackendException *e)
		{
			LOG(LOG_ERR, "EGEE Plugin (%s): failed to upload input "
				"files of job \"%s\": %s", name.c_str(), cjid,
				e->what());
			actJ->setStatus(Job::TEMPFAILED);
			cleanJobStorage(actJ);
			delete e;
			continue;
		}

		// Now add outputs
		jobJDLAd->setAttribute(JDL::STDOUTPUT, actJ->getId()+"std.out");
		jobJDLAd->addAttribute(JDL::OUTPUTSB, actJ->getId()+"std.out");
		jobJDLAd->addAttribute(JDL::OSB_DEST_URI, jobprefix + actJ->getId()+"std.out");
		jobJDLAd->setAttribute(JDL::STDERROR, actJ->getId()+"std.err");
		jobJDLAd->addAttribute(JDL::OUTPUTSB, actJ->getId()+"std.err");
		jobJDLAd->addAttribute(JDL::OSB_DEST_URI, jobprefix + actJ->getId()+"std.err");
		for (unsigned j = 0; j < outs->size(); j++)
		{
			jobJDLAd->addAttribute(JDL::OUTPUTSB, (*outs)[j]);
			jobJDLAd->addAttribute(JDL::OSB_DEST_URI, jobprefix + (*outs)[j]);
		}

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
		jdlFname << jdldir << "/" << setfill('0') << setw(4) << i;
		jdlFname << ".jdl";
		ofstream jobJDL(jdlFname.str().c_str());
		jobJDL << jobJDLAd->toString() << endl;
		if (jobJDL.bad())
		{
			LOG(LOG_ERR, "EGEE Plugin (%s): failed to create JDL "
				"file for job \"%s\": %s", name.c_str(), cjid,
				strerror(errno));
			jobJDL.close();
			actJ->setStatus(Job::TEMPFAILED);
			cleanJobStorage(actJ);
			continue;
		}
		jobJDL.close();
		LOG(LOG_DEBUG, "EGEE Plugin (%s): JDL for job \"%s\" is: %s",
			name.c_str(), cjid, jobJDLAd->toString().c_str());

		delete jobJDLAd;
		i++;
	}

	if (!i)
	{
		LOG(LOG_INFO, "EGEE Plugin (%s): due to the previous errors, "
			"there are no jobs to submit.", name.c_str());
		invoke_cmd("rm", rmargs, NULL);
		return;
	}

	// Submit the JDLs
	string stdoe;
	string collidfile = string(wdir) + "/collection.id";
	try
	{
		const char *submitargs[] = {"glite-wms-job-submit", "-a", "-e",
			wmpendp, "-o", collidfile.c_str(), "--collection",
			jdldir, NULL};
		if (invoke_cmd("glite-wms-job-submit", submitargs, &stdoe))
		{
			LOG(LOG_ERR, "EGEE Plugin (%s): job submission failed:"
				"\n%s", name.c_str(), stdoe.c_str());

			const char *rmargs[] = { "rm", "-rf", wdir, NULL };
			invoke_cmd("rm", rmargs, NULL);
			for (JobVector::iterator it = jobs.begin();
				it != jobs.end(); it++)
			{
				logjob((*it)->getId(), ERROR, "Job submission "
					"failed: " + stdoe);
				cleanJobStorage(*it);
			}
			return;
		}
	}
	catch (BackendException *e)
	{
		LOG(LOG_ERR, "EGEE Plugin (%s): job submission failed: %s",
			name.c_str(), e->what());
		invoke_cmd("rm", rmargs, NULL);
		for (JobVector::iterator it = jobs.begin(); it != jobs.end();
			it++)
			cleanJobStorage(*it);
		return;
	}

	// Find out collection's ID
	ifstream collIDf(collidfile.c_str());
	if (!collIDf.good())
	{
		LOG(LOG_ERR, "EGEE Plugin (%s): failed to open collection "
			"identifier: %s", name.c_str(), strerror(errno));
		invoke_cmd("rm", rmargs, NULL);
		for (JobVector::iterator it = jobs.begin(); it != jobs.end();
			it++)
			cleanJobStorage(*it);
		return;
	}
	string collID;
	do
	{
		collIDf >> collID;
	} while ("https://" != collID.substr(0, 8));

	invoke_cmd("rm", rmargs, NULL);

	LOG(LOG_DEBUG, "EGEE Plugin (%s): collection submitted, now determining "
		"EGEE identifiers of nodes", name.c_str());

	// Find out job's ID
	glite::lb::JobStatus stat;
	try
	{
		JobId jID(collID);
		glite::lb::Job tJob(jID);
		tJob.setParam(EDG_WLL_PARAM_X509_PROXY, tmpdir + "/proxy.voms");
		stat = tJob.status(tJob.STAT_CLASSADS|tJob.STAT_CHILDREN|
			tJob.STAT_CHILDSTAT);
	}
	catch (BaseException &e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to get child nodes "
			"of collection \"%s\". Status of jobs remains "
			"unchanged. EGEE exception follows:\n%s",
			name.c_str(), collID.c_str(), getEGEEErrMsg(e).c_str());
		for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
			cleanJobStorage(*it);
		return;
	}
	catch (glite::wmsutils::exception::Exception &e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to get child nodes "
			"of collection \"%s\". Status of jobs remains "
			"unchanged. EGEE exception follows:\n%s",
			name.c_str(), collID.c_str(), e.dbgMessage().c_str());
		for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
			cleanJobStorage(*it);
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
		unsigned tries = 0;
		while (!events.size() && tries < 3) {
			try {
				LOG(LOG_DEBUG, "EGEE Plugin (%s): collecting "
					"node IDs (attempt %zd of 3)",
					name.c_str(), tries);
				events = ctJob.log();
				tries++;
			} catch (...) {
				sleep(20);
				tries++;
			}
		}
		if (tries == 3 && !events.size())
		{
			LOG(LOG_WARNING, "EGEE Plugin (%s): failed to determine "
				"node IDs for 60 seconds, skipping submission "
				"and cleaning job storages", name.c_str());
			for (JobVector::iterator it = jobs.begin();
				it != jobs.end(); it++)
				cleanJobStorage(*it);
			break;
		}

		for (unsigned j = 0; j < events.size(); j++)
			if (events[j].type == events[j].REGJOB) {
				Ad tAd(events[j].getValString(events[j].JDL));
				childNodeName = tAd.getString(JDL::NODE_NAME);
				break;
			}

		for (JobVector::iterator it = jobs.begin(); it != jobs.end();
			it++)
			if ((*it)->getGridId() == childNodeName) {
				(*it)->setGridId(childIDs[i]);
				(*it)->setStatus(Job::RUNNING);
				LOG(LOG_INFO, "EGEE Plugin (%s): submitted job "
					"\"%s\", grid identifier is \"%s\"",
					name.c_str(), (*it)->getId().c_str(),
					(*it)->getGridId().c_str());
				break;
			}
	}
	LOG(LOG_INFO, "EGEE Plugin (%s): job submission finished",
		name.c_str());
}


void EGEEHandler::updateStatus(void) throw (BackendException *)
{
	LOG(LOG_DEBUG, "EGEE Plugin (%s): updating status of jobs",
		name.c_str());

	try
	{
		createCFG();
	}
	catch (BackendException *e)
	{
		LOG(LOG_ERR, "EGEE Plugin (%s): failed to create ConfigContext"
			", skipping job status update: %s", name.c_str(),
			e->what());
		delete e;
		return;
	}

	DBHandler *jobDB = DBHandler::get();
	jobDB->pollJobs(this, Job::RUNNING, Job::CANCEL);
	DBHandler::put(jobDB);

	LOG(LOG_DEBUG, "EGEE Plugin (%s): status update finished",
		name.c_str());
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



//
// Private methods
//


void EGEEHandler::init_ftp_client(globus_ftp_client_handle_t *ftp_handle,
	globus_ftp_client_handleattr_t *ftp_handle_attrs,
	globus_ftp_client_operationattr_t *ftp_op_attrs,
	globus_ftp_client_plugin_t *rst_pin)
{
	globus_result_t result;

	globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);

	result = globus_ftp_client_handleattr_init(ftp_handle_attrs);
	if (GLOBUS_SUCCESS != result)
		throw new BackendException("Failed to initialize GridFTP "
			"client handle attribute");
	result = globus_ftp_client_handle_init(ftp_handle, ftp_handle_attrs);
	if (GLOBUS_SUCCESS != result)
		throw new BackendException("Failed to initialize GridFTP "
			"client handle");
	result = globus_ftp_client_operationattr_init(ftp_op_attrs);
	if (GLOBUS_SUCCESS != result)
		throw new BackendException("Failed to initialize GridFTP "
			"client operation attribute");
	if (rst_pin)
	{
		result = globus_ftp_client_restart_plugin_init(rst_pin, 3, 0,
			NULL);
		if (GLOBUS_SUCCESS != result)
			throw new BackendException("Failed to initialize "
				"GridFTP restart plugin");
		result = globus_ftp_client_handleattr_add_plugin(ftp_handle_attrs,
			rst_pin);
		if (GLOBUS_SUCCESS != result)
			throw new BackendException("Failed to add restart "
				"plugin to FTP handle attributes");
	}

	globus_mutex_init(&lock, GLOBUS_NULL);
	globus_cond_init(&cond, GLOBUS_NULL);

	globus_err = false;
	done = GLOBUS_FALSE;

	if (globus_errmsg)
	{
		free(globus_errmsg);
		globus_errmsg = NULL;
	}
}


void EGEEHandler::destroy_ftp_client(globus_ftp_client_handle_t *ftp_handle,
	globus_ftp_client_handleattr_t *ftp_handle_attrs,
	globus_ftp_client_operationattr_t *ftp_op_attrs,
	globus_ftp_client_plugin_t *rst_pin)
{
	globus_ftp_client_handleattr_destroy(ftp_handle_attrs);
	if (rst_pin)
		globus_ftp_client_restart_plugin_destroy(rst_pin);
	globus_ftp_client_operationattr_destroy(ftp_op_attrs);
	globus_ftp_client_handle_destroy(ftp_handle);
	globus_cond_destroy(&cond);
	globus_mutex_destroy(&lock);
}


void EGEEHandler::handle_finish(void *user_args, globus_ftp_client_handle_t *ftp_handle, globus_object_t *error)
{
	if (error)
		set_globus_err(error);

	globus_mutex_lock(&lock);
	done = GLOBUS_TRUE;
	globus_cond_signal(&cond);
	globus_mutex_unlock(&lock);
}


void EGEEHandler::set_globus_err(globus_object_t *error)
{
	globus_errmsg = globus_error_print_chain(error);
	if (!globus_errmsg)
		globus_errmsg = strdup("Unknown error (probably timeout)");
	globus_err = true;
}


void EGEEHandler::transfer_files_globus(const vector<string> &srcFiles, const vector<string> &dstFiles) throw(BackendException *)
{
	bool transproblem = false;
	globus_result_t result;
	globus_ftp_client_handle_t ftp_handle;
	globus_ftp_client_handleattr_t ftp_handle_attrs;
	globus_ftp_client_operationattr_t ftp_op_attrs;
	globus_gass_copy_handle_t g_c_h;
	globus_gass_copy_attr_t sattr, dattr;
	globus_gass_copy_handleattr_t g_c_h_a;
	globus_ftp_client_operationattr_t sfa, dfa;
	globus_ftp_client_plugin_t rst_pin;

	globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
	globus_module_activate(GLOBUS_FTP_CLIENT_RESTART_PLUGIN_MODULE);
	globus_module_activate(GLOBUS_GASS_COPY_MODULE);
	init_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs, &rst_pin);

	if (srcFiles.size() != dstFiles.size())
		LOG(LOG_WARNING, "EGEE Plugin (%s): sizes of source and destination "
			"file vectors for transfer do not match!", name.c_str());
	unsigned fnum = (srcFiles.size() < dstFiles.size() ? srcFiles.size() :
		dstFiles.size());

	try
	{
		result = globus_gass_copy_handleattr_init(&g_c_h_a);
		if (GLOBUS_SUCCESS != result)
		{
			set_globus_err(globus_error_get(result));
			throw new BackendException("globus_gass_copy_handleattr"
				"_init() failed : %s", globus_errmsg);
		}
		result |= globus_gass_copy_handleattr_set_ftp_attr(&g_c_h_a,
			&ftp_handle_attrs);
		result |= globus_gass_copy_handle_init(&g_c_h, &g_c_h_a);

		result |= globus_gass_copy_attr_init(&sattr);
		result |= globus_gass_copy_attr_init(&dattr);

		result |= globus_ftp_client_operationattr_init(&sfa);
		result |= globus_ftp_client_operationattr_init(&dfa);
		if (GLOBUS_SUCCESS != result)
		{
			set_globus_err(globus_error_get(result));
			throw new BackendException("Globus GASS copy setup "
				"failed : %s", globus_errmsg);
		}

		result |= globus_gass_copy_attr_set_ftp(&sattr, &sfa);
		result |= globus_gass_copy_attr_set_ftp(&dattr, &dfa);
		if (GLOBUS_SUCCESS != result)
		{
			set_globus_err(globus_error_get(result));
			throw new BackendException("Globus GASS copy setup "
				"failed : %s", globus_errmsg);
		}

		for (unsigned i = 0; i < fnum; i++)
		{
			const char *src = srcFiles[i].c_str();
			const char *dst = dstFiles[i].c_str();

			LOG(LOG_DEBUG, "EGEE Plugin (%s): transferring \"%s\" "
				"to \"%s\"", name.c_str(), src, dst);
			result = globus_gass_copy_url_to_url(&g_c_h,
				(char *)src, &sattr, (char *)dst, &dattr);
			if (result)
			{
				transproblem = true;
				LOG(LOG_ERR, "EGEE Plugin (%s): failed to "
					"transfer from %s to %s!", name.c_str(),
					src, dst);
			}
		}
	}
	catch (BackendException *e)
	{
		globus_ftp_client_operationattr_destroy(&sfa);
		globus_ftp_client_operationattr_destroy(&dfa);
		globus_gass_copy_handle_destroy(&g_c_h);
		destroy_ftp_client(&ftp_handle, &ftp_handle_attrs,
			&ftp_op_attrs, &rst_pin);
		throw e;
	}
	globus_ftp_client_operationattr_destroy(&sfa);
	globus_ftp_client_operationattr_destroy(&dfa);
	globus_gass_copy_handle_destroy(&g_c_h);
	destroy_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs, &rst_pin);
	if (transproblem)
		throw new BackendException("Some file transfer(s) failed!");
}


void EGEEHandler::delete_files_globus(const vector<string> &fileNames, const string &prefix)
{
	globus_result_t result;

	for (vector<string>::const_iterator it = fileNames.begin();
		it != fileNames.end(); it++)
	{
		globus_ftp_client_handle_t ftp_handle;
		globus_ftp_client_handleattr_t ftp_handle_attrs;
		globus_ftp_client_operationattr_t ftp_op_attrs;

		globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
		init_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);

		string fpath = *it;
		string bname = fpath.substr((fpath.rfind("/") == string::npos ?
			0 : fpath.rfind("/")+1), string::npos);
		string rname = prefix + "/" + bname;
		globus_err = false;
		done = GLOBUS_FALSE;
		LOG(LOG_DEBUG, "EGEE Plugin (%s): about to remove remote file "
			"\"%s\"", name.c_str(), rname.c_str());
		result = globus_ftp_client_delete(&ftp_handle, rname.c_str(),
			&ftp_op_attrs,
			(globus_ftp_client_complete_callback_t)handle_finish,
			NULL);
		if (GLOBUS_SUCCESS != result)
		{
			set_globus_err(globus_error_get(result));
			LOG(LOG_WARNING, "EGEE Plugin (%s): failed to remove "
				"file \"%s\": %s", name.c_str(), rname.c_str(),
				globus_errmsg);
			destroy_ftp_client(&ftp_handle, &ftp_handle_attrs,
				&ftp_op_attrs);
			continue;
		}
		globus_mutex_lock(&lock);
		while(!done)
			globus_cond_wait(&cond, &lock);
		globus_mutex_unlock(&lock);
		if (globus_err)
			LOG(LOG_WARNING, "EGEE Plugin (%s): failed to remove "
				"file \"%s\": %s", name.c_str(), rname.c_str(),
				globus_errmsg);
		destroy_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);
	}
}


void EGEEHandler::create_dir_globus(const string &dirurl) throw (BackendException *)
{
	globus_result_t result;
	globus_ftp_client_handle_t ftp_handle;
	globus_ftp_client_handleattr_t ftp_handle_attrs;
	globus_ftp_client_operationattr_t ftp_op_attrs;

	globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
	init_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);

	result = globus_ftp_client_mkdir(&ftp_handle, dirurl.c_str(),
		&ftp_op_attrs,
		(globus_ftp_client_complete_callback_t)handle_finish, NULL);
	if (GLOBUS_SUCCESS != result)
	{
		set_globus_err(globus_error_get(result));
		destroy_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);
		throw new BackendException("Failed to create remote "
			"directory \"" + dirurl + "\" using GridFTP: " +
			string(globus_errmsg));
	}

	globus_mutex_lock(&lock);
	while (!done)
		globus_cond_wait(&cond, &lock);
	globus_mutex_unlock(&lock);

	destroy_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);
	if (globus_err)
		throw new BackendException("Failed to create remote "
			"directory \"" + dirurl + "\" using GridFTP: " +
			string(globus_errmsg));
}


void EGEEHandler::remove_dir_globus(const string &dirurl)
{
	globus_result_t result;
	globus_ftp_client_handle_t ftp_handle;
	globus_ftp_client_handleattr_t ftp_handle_attrs;
	globus_ftp_client_operationattr_t ftp_op_attrs;

	globus_module_activate(GLOBUS_FTP_CLIENT_MODULE);
	init_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);

	result = globus_ftp_client_rmdir(&ftp_handle, dirurl.c_str(),
		&ftp_op_attrs,
		(globus_ftp_client_complete_callback_t)handle_finish, NULL);
	if (GLOBUS_SUCCESS != result)
	{
		set_globus_err(globus_error_get(result));
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to remove remote "
			"directory \"%s\" using GridFTP: %s", name.c_str(),
			dirurl.c_str(), globus_errmsg);
		destroy_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);
		return;
	}

	globus_mutex_lock(&lock);
	while (!done)
		globus_cond_wait(&cond, &lock);
	globus_mutex_unlock(&lock);

	destroy_ftp_client(&ftp_handle, &ftp_handle_attrs, &ftp_op_attrs);
	if (globus_err)
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to remove remote "
			"directory \"%s\" using GridFTP: %s", name.c_str(),
			dirurl.c_str(), globus_errmsg);
}


void EGEEHandler::cleanJob(Job *job)
{
	unsigned i = 0;
	string jobID = job->getGridId();
	LOG(LOG_INFO, "EGEE Plugin (%s): cleaning job \"%s\" (EGEE ID is \"%s\"",
		name.c_str(), job->getId().c_str(), jobID.c_str());
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
			LOG(LOG_WARNING, "EGEE Plugin (%s): clean attempt %zd "
				"failed, EGEE exception follows:\n%s",
				name.c_str(), i, getEGEEErrMsg(e).c_str());
		}
	}
	cleanJobStorage(job);
}


void EGEEHandler::cleanJobStorage(Job *job)
{
	vector<string> cleanFiles;
	auto_ptr<vector<string> >  ins = job->getInputs();
	auto_ptr<vector<string> > outs = job->getOutputs();
	string jobprefix = string(isb_url) + "/" + job->getId() + "/";

	for (unsigned j = 0; j < ins->size(); j++)
		cleanFiles.push_back((*ins)[j]);
	for (unsigned j = 0; j < outs->size(); j++)
		cleanFiles.push_back((*outs)[j]);
	cleanFiles.push_back(jobprefix + job->getId()+"std.out");
	cleanFiles.push_back(jobprefix + job->getId()+"std.err");
	delete_files_globus(cleanFiles, jobprefix);
	remove_dir_globus(jobprefix);
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


void EGEEHandler::renew_proxy() throw(BackendException *)
{
	time_t lifetime;
	string proxyf = tmpdir + "/proxy";
	string vproxyf = tmpdir + "/proxy.voms";
        myproxy_request_t *req;
        myproxy_socket_attrs_t *attrs;
        myproxy_response_t *resp;

	getProxyInfo(vproxyf.c_str(), &lifetime);
	// Do not update proxy if it is valid for at least 6 hrs
	if (lifetime > 6*60*60) {
		setenv("X509_USER_PROXY", vproxyf.c_str(), 1);
		return;
	}

	LOG(LOG_DEBUG, "EGEE Plugin (%s): about to renew proxy",
		name.c_str());
	setenv("X509_USER_CERT", myproxy_authcert, 1);
	setenv("X509_USER_KEY", myproxy_authkey, 1);

        req = (myproxy_request_t *)malloc(sizeof(*req));
        memset(req, 0, sizeof(myproxy_request_t));
        req->version = strdup(MYPROXY_VERSION);
        req->command_type = MYPROXY_GET_PROXY;
        req->proxy_lifetime = 60*60*12;
        attrs = (myproxy_socket_attrs_t *)malloc(sizeof(*attrs));
        memset(attrs, 0, sizeof(*attrs));
        attrs->pshost = strdup(myproxy_host);
        attrs->psport = atoi(myproxy_port);
        resp = (myproxy_response_t *)malloc(sizeof(*resp));
        memset(resp, 0, sizeof(*resp));
	req->username = strdup(myproxy_user);

        if (myproxy_get_delegation(attrs, req, NULL, resp, (char *)proxyf.c_str()))
        {
		myproxy_free(attrs, req, resp);
                if (verror_is_error())
			throw new BackendException("Proxy initialization "
				"failed: " + string(verror_get_string()));
		else
			throw new BackendException("Proxy initialization "
				"failed due to an unknown error");
        }
	myproxy_free(attrs, req, resp);
	unsetenv("X509_USER_CERT");
	unsetenv("X509_USER_KEY");

	setenv("X509_USER_PROXY", proxyf.c_str(), 1);
	const char *vpiargs[] = { "voms-proxy-init", "-voms", voname, "-noregen",
		"-out", vproxyf.c_str(), "-valid", "11:00"};
	string vpiout;
	if (invoke_cmd("voms-proxy-init", vpiargs, &vpiout))
	{
		unlink(proxyf.c_str());
		throw new BackendException("Adding VOMS extensions to proxy "
			"failed: " + vpiout);
	}

	unlink(proxyf.c_str());
	setenv("X509_USER_PROXY", vproxyf.c_str(), 1);
	LOG(LOG_DEBUG, "EGEE Plugin (%s): proxy renewal finished", name.c_str());
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
		LOG(LOG_ERR, "EGEE Plugin (%s): job result download of job "
			"\"%s\" failed as ConfigContext creation failed: %s",
			name.c_str(), job->getId().c_str(), e->what());
		delete e;
		return;
	}

	LOG(LOG_INFO, "EGEE Plugin (%s): fetching output of job \"%s\" (EGEE "
		"ID is \"%s\")", name.c_str(), job->getId().c_str(),
		job->getGridId().c_str());

	try
	{
		vector<string> remFiles;
		vector<string> locFiles;
		auto_ptr<vector<string> > outs = job->getOutputs();
		string jobprefix = string(isb_url) + "/" + job->getId() + "/";
		string outtpath = string(tmpdir)+"/"+job->getId()+"std.out";
		string errtpath = string(tmpdir)+"/"+job->getId()+"std.err";

		remFiles.push_back(jobprefix + job->getId() + "std.out");
		locFiles.push_back("file://" + outtpath);
		remFiles.push_back(jobprefix + job->getId() + "std.err");
		locFiles.push_back("file://" + errtpath);
		for (unsigned i = 0; i < outs->size(); i++)
		{
			remFiles.push_back(jobprefix + (*outs)[i]);
			locFiles.push_back("file://" +
				job->getOutputPath((*outs)[i]));
		}
		try
		{
			transfer_files_globus(remFiles, locFiles);
		}
		catch (BackendException *e)
		{
			LOG(LOG_WARNING, "EGEE Plugin (%s): some file transfers"
				" failed. Please check above messages for "
				"details.", name.c_str());
			delete e;
		}

		// If job logging level isn't NONE, preserve stdout and stderr
		if (jobloglevel != NONE)
		{
			string no = string(joblogdir)+"/"+job->getId()+".out";
			string ne = string(joblogdir)+"/"+job->getId()+".err";
			if (-1 == rename(outtpath.c_str(), no.c_str()))
				LOG(LOG_WARNING, "Failed to rename stdout: %s", strerror(errno));
			if (-1 == rename(errtpath.c_str(), ne.c_str()))
				LOG(LOG_WARNING, "Failed to rename stderr: %s", strerror(errno));
		}
		cleanJob(job);
	}
	catch (BaseException &e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to get output file "
			"list, I assume the job has already been fetched, but "
			"EGEE exception is here for your convenience: ",
			name.c_str());
		LOG(LOG_WARNING, getEGEEErrMsg(e).c_str());
		cleanJobStorage(job);
	}
	catch (BackendException *e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): cleaning job \"%s\" failed.",
			name.c_str(), job->getGridId().c_str());
		delete e;
		cleanJobStorage(job);
	}
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
		throw new BackendException("Failed to create ConfigContext for "
			"unknown reason");
}


void EGEEHandler::getProxyInfo(const char *proxyfile, time_t *lifetime)
{
	*lifetime = 0;

	if (!proxyfile)
		return;

	globus_module_activate(GLOBUS_GSI_CREDENTIAL_MODULE);

	globus_gsi_cred_handle_t handle;
	globus_gsi_cred_handle_attrs_t attrs;

	try
	{
		if (GLOBUS_SUCCESS != globus_gsi_cred_handle_attrs_init(&attrs))
			throw new BackendException("a");

		if (GLOBUS_SUCCESS != globus_gsi_cred_handle_init(&handle, attrs))
			throw new BackendException("a");

		if (GLOBUS_SUCCESS != globus_gsi_cred_read_proxy(handle, proxyfile))
			throw new BackendException("a");

		if (GLOBUS_SUCCESS != globus_gsi_cred_get_lifetime(handle, lifetime))
			throw new BackendException("a");
	}
	catch (BackendException *e)
	{
		delete e;
	}

	globus_gsi_cred_handle_destroy(handle);
	globus_gsi_cred_handle_attrs_destroy(attrs);
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

	unsigned tries = 0;
	string statStr = "";
	int statCode = 0;
	int exitCode = 0;
	logjob(job->getId(), ALL, "Updating status of job " + job->getGridId());
	const char *gstat[] = {"glite-wms-job-status", "-v", "3",
		job->getGridId().c_str()};
	while (tries < 3)
	{
		try
		{
			JobId jID(job->getGridId());
			glite::lb::Job tJob(jID);
			tJob.setParam(EDG_WLL_PARAM_X509_PROXY, tmpdir +
				"/proxy.voms");
			glite::lb::JobStatus stat = tJob.status(tJob.STAT_CLASSADS);
			statStr = stat.name();
			statCode = stat.getValInt(glite::lb::JobStatus::DONE_CODE);
			exitCode = stat.getValInt(glite::lb::JobStatus::EXIT_CODE);
			tries = 3;
		}
		catch (BaseException &e)
		{
			string gstatstr;
			invoke_cmd("glite-wms-job-status", gstat, &gstatstr);
			logjob(job->getId(), ERROR, "Failed to update status"
				"of job " + job->getGridId() + ": " + getEGEEErrMsg(e));
			logjob(job->getId(), ERROR, "CLI-based status query:" +
				gstatstr);
			LOG(LOG_WARNING, "EGEE Plugin (%s): failed to update "
				"status of EGEE job \"%s\" (attempt %zd), EGEE "
				"exception follows:\n%s", name.c_str(),
				job->getGridId().c_str(), ++tries,
				getEGEEErrMsg(e).c_str());
			if (tries >= 3)
			{
				LOG(LOG_ERR, "EGEE Plugin (%s): status update "
					"of job \"%s\" (EGEE ID is \"%s\") "
					"three times, the job will be aborted",
					name.c_str(), job->getId().c_str(),
					job->getGridId().c_str());
				cancelJob(job, false);
				job->setStatus(Job::ERROR);
				return;
			}
			sleep(5);
		}
		catch (glite::wmsutils::exception::Exception &e)
		{
			string gstatstr;
			invoke_cmd("glite-wms-job-status", gstat, &gstatstr);
			logjob(job->getId(), ERROR, "Failed to update status"
				"of job " + job->getGridId() + ": " + e.dbgMessage());
			logjob(job->getId(), ERROR, "CLI-based status query:" +
				gstatstr);
			LOG(LOG_WARNING, "EGEE Plugin (%s): failed to update "
				"status of EGEE job \"%s\" (attempt %zd), EGEE "
				"exception follows:\n%s", name.c_str(),
				job->getGridId().c_str(), ++tries,
				e.dbgMessage().c_str());
			if (tries >= 3)
			{
				LOG(LOG_ERR, "EGEE Plugin (%s): status update "
					"of job \"%s\" (EGEE ID is \"%s\") "
					"three times, the job will be aborted",
					name.c_str(), job->getId().c_str(),
					job->getGridId().c_str());
				cancelJob(job, false);
				job->setStatus(Job::ERROR);
				return;
			}
			sleep(5);
		}
	}

	logjob(job->getId(), ALL, "Updated status of job " + job->getGridId() + " is: " + statStr);
	LOG(LOG_DEBUG, "EGEE Plugin (%s): updated EGEE status of job \"%s\" "
		"(EGEE identifier is \"%s\"): %s", name.c_str(),
		job->getId().c_str(), job->getGridId().c_str(), statStr.c_str());

	string linfostr;
	const char *linfo[] = {"glite-wms-job-logging-info", "-v", "3",
		job->getGridId().c_str(), NULL};
	for (unsigned j = 0; statusRelation[j].EGEEs != ""; j++)
	{
		if (statusRelation[j].EGEEs == statStr)
		{
			if (Job::FINISHED == statusRelation[j].jobS)
			{
				LOG(LOG_DEBUG, "EGEE Plugin (%s): exit code of"
					" EGEE job \"%s\" is: %d", name.c_str(),
					job->getGridId().c_str(), exitCode);
				getOutputs_real(job);
				if (glite::lb::JobStatus::DONE_CODE_OK !=
					statCode || 0 != exitCode)
				{
					invoke_cmd("glite-wms-job-logging-info",
						linfo, &linfostr);
					logjob(job->getId(), ALL, "Detailed job"
						" logging info: " + linfostr);
				}
				else
				{
					if (jobloglevel == ERROR)
					{
						string lfn = joblogdir + string("/") +
							job->getId() + string(".log");
						string efn = joblogdir + string("/") +
							job->getId() + string(".err");
						string ofn = joblogdir + string("/") +
							job->getId() + string(".out");
						unlink(lfn.c_str());
						unlink(efn.c_str());
						unlink(ofn.c_str());
					}
				}
			}
			job->setStatus(statusRelation[j].jobS);
			if (Job::ERROR == statusRelation[j].jobS)
			{
				invoke_cmd("glite-wms-job-logging-info",
					linfo, &linfostr);
				logjob(job->getId(), ALL, "Detailed job "
					"logging info: " + linfostr);
				cleanJobStorage(job);
			}
		}
	}
}


void EGEEHandler::cancelJob(Job *job, bool clean)
{
	LOG(LOG_DEBUG, "EGEE Plugin (%s): about to cancel and remove job \"%s\""
		" (EGEE ID is \"%s\")", name.c_str(), job->getId().c_str(),
		job->getGridId().c_str());
	try
	{
		jobCancel(job->getGridId(), cfg);
	}
	catch (BaseException &e)
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to cancel job "
			"\"%s\", EGEE exception follows:", name.c_str(),
			job->getGridId().c_str());
		LOG(LOG_WARNING, getEGEEErrMsg(e).c_str());
	}

	cleanJob(job);

	if (clean)
	{
		DBHandler *jobDB = DBHandler::get();
		jobDB->deleteJob(job->getId());
		DBHandler::put(jobDB);
	}
}


void EGEEHandler::logjob(const string& jobid, const int level, const string& msg)
{
	char datestr[256];

	if (level < jobloglevel)
		return;

	string lfn = joblogdir + string("/") + jobid + string(".log");
	ofstream lf;
	lf.open(lfn.c_str(), ios::out|ios::app);
	if (!lf.is_open())
	{
		LOG(LOG_WARNING, "EGEE Plugin (%s): failed to open job log file"
			" \"%s\" for writing.", name.c_str(), lfn.c_str());
		return;
	}

	time_t curtime = time(NULL);
	struct tm* lt = localtime(&curtime);
	strftime(datestr, 255, "%F %T: ", lt);
	lf << string(datestr) << msg << endl;
	lf.close();
}

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new EGEEHandler(config, instance);
}
