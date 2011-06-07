/*
 * Copyright (C) 2008-2010 MTA SZTAKI LPDS
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

#include "DC-API-Single_handler.h"
#include "DBHandler.h"
#include "Job.h"
#include "Util.h"
#include "DLException.h"

#include <dc.h>

#include <set>
#include <vector>
#include <iostream>
#include <fstream>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>


using namespace std;

/* Custom project URL defined in config file */
static gchar *projecturl;

static void result_callback_single(DC_Workunit *wu, DC_Result *result)
{
	char *tmp = DC_serializeWU(wu);
	string wuid(tmp);
	free(tmp);

	tmp = DC_getWUTag(wu);
	string jobid(tmp);
	free(tmp);

	LOG(LOG_DEBUG, "DC-API-Single: WU %s: result callback for job id %s", wuid.c_str(), jobid.c_str());

	DBHandler *dbh = DBHandler::get();
	auto_ptr<Job> job = dbh->getJob(jobid.c_str());
	DBHandler::put(dbh);

	if (!job.get())
	{
		LOG(LOG_ERR, "DC-API-Single: WU %s: No matching job entry in the job database", wuid.c_str());
		DC_destroyWU(wu);
		return;
	}

	if (!result)
	{
		LOG(LOG_ERR, "DC-API-Single: WU %s: Failed", wuid.c_str());
		job->setStatus(Job::ERROR);
		DC_destroyWU(wu);
		return;
	}

        auto_ptr< vector<string> > outputs = job->getOutputs();
	for (vector<string>::iterator it = outputs->begin(); it != outputs->end(); it++)
	{
		tmp = DC_getResultOutput(result, (*it).c_str());
		if (!tmp)
		{
			LOG(LOG_ERR, "DC-API-Single: WU %s: Missing output file %s",
				wuid.c_str(), (*it).c_str());
			job->setStatus(Job::ERROR);
			DC_destroyWU(wu);
			return;
		}
		if (link(tmp, job->getOutputPath(*it).c_str()))
		{
			LOG(LOG_ERR, "DC-API-Single: WU %s: Failed to link output file %s to %s",
				wuid.c_str(), tmp, job->getOutputPath(*it).c_str());
			job->setStatus(Job::ERROR);
			DC_destroyWU(wu);
			free(tmp);
			return;
		}
		free(tmp);
	}

	job->setStatus(Job::FINISHED);
	LOG(LOG_INFO, "DC-API-Single: WU %s: Result received", wuid.c_str());
	logit_mon("event=job_status job_id=%s status=Finished", job->getId().c_str());
	DC_destroyWU(wu);
}

/**********************************************************************
 * Class: DCAPIHandler
 */

DCAPISingleHandler::DCAPISingleHandler(GKeyFile *config, const char *instance) throw (BackendException *): GridHandler(config, instance)
{
	name = instance;

	gchar *conffile = g_key_file_get_string(config, instance, "dc-api-config", NULL);
	if (conffile)
		g_strstrip(conffile);

	if (DC_OK != DC_initMaster(conffile ? conffile : NULL))
		throw new BackendException("Failed to initialize the DC-API");

	g_free(conffile);

	projecturl = g_key_file_get_string(config, instance, "project-url", NULL);
	if (!projecturl)
		projecturl = g_strdup("undefined");

	DC_setMasterCb(result_callback_single, NULL, NULL);

	groupByNames = false;
}

static bool submit_job(Job *job) throw (BackendException *)
{
	GError *error = 0;
	char **argv = NULL;
	int argc;

	LOG(LOG_DEBUG, "DC-API Submit job: '%s'", job->getId().c_str());

	if (NULL == job->getAlgQueue())
	{
		LOG(LOG_ERR, "DC-API-Single: Job %s: unknown algorithm queue",
			job->getId().c_str());
		return false;
	}

	if (!g_shell_parse_argv(job->getArgs().c_str(), &argc, &argv, &error))
	{
		if (G_SHELL_ERROR_EMPTY_STRING != error->code)
		{
			LOG(LOG_ERR, "DC-API-Single: Job %s: Failed to parse the arguments: %s",
				job->getId().c_str(), error->message);
			g_error_free(error);
			return false;
		}
	}

	const char *algName = job->getAlgQueue()->getName().c_str();
	DC_Workunit *wu = DC_createWU(algName, (const char **)argv, 0, job->getId().c_str());
	g_strfreev(argv);
	if (!wu)
	{
		LOG(LOG_ERR, "DC-API-Single: Job %s: DC_createWU() failed", job->getId().c_str());
		return false;
	}

	auto_ptr< vector<string> > inputs = job->getInputs();

	/* Check for unsupported URLs */
	DLException *dle = NULL;
	for (vector<string>::iterator it = inputs->begin(); it != inputs->end(); it++)
	{
		FileRef fr = job->getInputRef(*it);
		string url = fr.getURL();
		string md5 = fr.getMD5() ? string(fr.getMD5()) : string();
		int size = fr.getSize();
		if ("http://" != url.substr(0, 7) && "attic://" != url.substr(0, 8) && '/' != url[0] ||
			(("http://" == url.substr(0, 7) || "attic://" == url.substr(0, 8)) && (md5 == "" || size == -1)))
		{
			if (!dle)
				dle = new DLException(job->getId());
			dle->addInput(*it);
		}
	}
	if (dle)
	{
		DC_destroyWU(wu);
		throw dle;
	}

	for (vector<string>::iterator it = inputs->begin(); it != inputs->end(); it++)
	{
		string lname = *it;
		FileRef fr = job->getInputRef(lname);
		string url = fr.getURL();
		string md5 = fr.getMD5() ? string(fr.getMD5()) : string();
		int size = fr.getSize();
		
		if ("http://" == url.substr(0, 7) || "attic://" == url.substr(0, 8))
		{
			LOG(LOG_DEBUG, "DC-API-Single: adding remote input file %s to workunit as %s.",
				url.c_str(), lname.c_str());
			if (DC_addWUInput(wu, lname.c_str(), url.c_str(), DC_FILE_REMOTE, md5.c_str(), size))
			{
				LOG(LOG_ERR, "DC-API-Single: failed to add input file %s to WU of job %s!",
					lname.c_str(), job->getId().c_str());
				DC_destroyWU(wu);
				return false;
			}
		}
		else if (DC_addWUInput(wu, lname.c_str(), url.c_str(), DC_FILE_VOLATILE))
		{
			LOG(LOG_ERR, "DC-API-Single: Job %s: Failed to add input file \"%s\"",
				job->getId().c_str(), url.c_str());
			DC_destroyWU(wu);
			return false;
		}
		LOG(LOG_DEBUG, "DC-API-Single: Input file \"%s\" added to WU of job \"%s\" as \"%s\"",
			url.c_str(), job->getId().c_str(), lname.c_str());
	}

	auto_ptr< vector<string> > outputs = job->getOutputs();
	for (vector<string>::iterator it = outputs->begin(); it != outputs->end(); it++)
	{
		if (DC_addWUOutput(wu, (*it).c_str()))
		{
			LOG(LOG_ERR, "DC-API-Single: Job %s: Failed to add output file \"%s\"",
				job->getId().c_str(), (*it).c_str());
			DC_destroyWU(wu);
			return false;
		}
		LOG(LOG_DEBUG, "DC-API-Single: Output file \"%s\" added to WU of job \"%s\"",
			(*it).c_str(), job->getId().c_str());
	}

	//If "_3G_BRIDGE_BATCH_ID" exists among the environment variables, set it as batch of the WU
	const char *_3gbatch = job->getEnv("_3G_BRIDGE_BATCH_ID").c_str();
	if (strlen(_3gbatch)>0)
	{
		LOG(LOG_DEBUG, "DC-API-Single: _3G_BRIDGE_BATCH_ID = \"%s\"",_3gbatch);
		DC_setWUBatch(wu,atoi(_3gbatch));
	}

	if (DC_submitWU(wu))
	{
		LOG(LOG_ERR, "DC-API-Single: Job %s: Failed to submit", job->getId().c_str());
		DC_destroyWU(wu);
		return false;
	}

	char *wu_id = DC_serializeWU(wu);
	char *wu_llid = DC_getWUId(wu);
	job->setGridId(wu_id);
	string wuURL = string(projecturl) + "/workunit.php?wuid=" + wu_llid;
 	job->setGridData(wuURL.c_str());
	job->setStatus(Job::RUNNING);

	LOG(LOG_INFO, "DC-API: WU %s: Submitted to grid %s (app '%s', job %s)",
		wu_id, job->getGrid().c_str(), algName, job->getId().c_str());
	logit_mon("event=job_submission job_id=%s job_id_dg=%s output_grid_name=boinc/%s",
		job->getId().c_str(), wu_id, projecturl);
	logit_mon("event=job_status job_id=%s status=Running", job->getId().c_str());

	free(wu_id);
	return true;
}

void DCAPISingleHandler::submitJobs(JobVector &jobs) throw (BackendException *)
{
	for (JobVector::iterator i = jobs.begin(); i != jobs.end(); i++)
	{
		if (!submit_job(*i))
			(*i)->setStatus(Job::ERROR);
	}
}

void DCAPISingleHandler::updateStatus(void) throw (BackendException *)
{
	DBHandler *dbh = DBHandler::get();

	/* Cancel WUs where all the contained tasks are in state CANCEL */
	vector<string> ids;
	dbh->getCompleteWUsSingle(ids, name, Job::CANCEL);
	for (vector<string>::const_iterator it = ids.begin(); it != ids.end(); it++)
	{
		DC_Workunit *wu;

		wu = DC_deserializeWU(it->c_str());
		if (wu)
		{
			LOG(LOG_DEBUG, "DC-API-Single: WU %s: Cancelling", it->c_str());
			DC_cancelWU(wu);
			DC_destroyWU(wu);
		}
		dbh->deleteBatch(*it);
	}

	DBHandler::put(dbh);

	int ret = DC_processMasterEvents(0);
	if (ret && ret != DC_ERR_TIMEOUT)
		throw new BackendException("DC_processMasterEvents() returned failure");
}

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new DCAPISingleHandler(config, instance);
}
