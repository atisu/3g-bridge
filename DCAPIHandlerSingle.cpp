#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "DCAPIHandlerSingle.h"
#include "DBHandler.h"
#include "Job.h"
#include "Util.h"

#include <dc.h>

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


static void result_callback_single(DC_Workunit *wu, DC_Result *result)
{
	char *tmp = DC_serializeWU(wu);
	string wuid(tmp);
	free(tmp);

	tmp = DC_getWUTag(wu);
	string jobid(tmp);
	free(tmp);

	LOG(LOG_DEBUG, "DC-API-SINGLE: WU %s: result callback for job id %s", wuid.c_str(), jobid.c_str());

	DBHandler *dbh = DBHandler::get();
	auto_ptr<Job> job = dbh->getJob(jobid.c_str());
	DBHandler::put(dbh);

	if (!job.get())
	{
		LOG(LOG_ERR, "DC-API-SINGLE: WU %s: No matching job entry in the job database", wuid.c_str());
		DC_destroyWU(wu);
		return;
	}

	if (!result)
	{
		LOG(LOG_ERR, "DC-API-SINGLE: WU %s: Failed", wuid.c_str());
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
			LOG(LOG_ERR, "DC-API-SINGLE: WU %s: Missing output file %s",
				wuid.c_str(), (*it).c_str());
			job->setStatus(Job::ERROR);
			DC_destroyWU(wu);
			return;
		}
		if (link(tmp, job->getOutputPath(*it).c_str()))
		{
			LOG(LOG_ERR, "DC-API-SINGLE: WU %s: Failed to link output file %s to %s",
				wuid.c_str(), tmp, job->getOutputPath(*it).c_str());
			job->setStatus(Job::ERROR);
			DC_destroyWU(wu);
			free(tmp);
			return;
		}
		free(tmp);
	}

	job->setStatus(Job::FINISHED);
	LOG(LOG_INFO, "DC-API-SINGLE: WU %s: Result received", wuid.c_str());
	DC_destroyWU(wu);
}

/**********************************************************************
 * Class: DCAPIHandler
 */

DCAPIHandlerSingle::DCAPIHandlerSingle(GKeyFile *config, const char *instance)
{
	name = instance;

	char *conffile = g_key_file_get_string(config, instance, "dc-api-config", NULL);

	if (DC_OK != DC_initMaster(conffile ? conffile : NULL))
		throw new BackendException("Failed to initialize the DC-API");

	g_free(conffile);

	DC_setMasterCb(result_callback_single, NULL, NULL);

	groupByNames = false;
}


DCAPIHandlerSingle::~DCAPIHandlerSingle()
{
}


static bool submit_job(Job *job)
{
	GError *error = 0;
	char **argv = NULL;
	int argc;

	if (!g_shell_parse_argv(job->getArgs().c_str(), &argc, &argv, &error))
	{
		if (G_SHELL_ERROR_EMPTY_STRING != error->code)
		{
			LOG(LOG_ERR, "DC-API-SINGLE: Job %s: Failed to parse the arguments: %s",
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
		LOG(LOG_ERR, "DC-API-SINGLE: Job %s: DC_createWU() failed", job->getId().c_str());
		return false;
	}

	auto_ptr< vector<string> > inputs = job->getInputs();
	for (vector<string>::iterator it = inputs->begin(); it != inputs->end(); it++)
	{
		if (DC_addWUInput(wu, (*it).c_str(), job->getInputPath(*it).c_str(), DC_FILE_VOLATILE))
		{
			LOG(LOG_ERR, "DC-API-SINGLE: Job %s: Failed to add input file \"%s\"",
				job->getId().c_str(), job->getInputPath(*it).c_str());
			DC_destroyWU(wu);
			return false;
		}
		LOG(LOG_DEBUG, "DC-API-SINGLE: Input file \"%s\" added to WU of job \"%s\" as \"%s\"",
			job->getInputPath(*it).c_str(), job->getId().c_str(), (*it).c_str());
	}

	auto_ptr< vector<string> > outputs = job->getOutputs();
	for (vector<string>::iterator it = outputs->begin(); it != outputs->end(); it++)
	{
		if (DC_addWUOutput(wu, (*it).c_str()))
		{
			LOG(LOG_ERR, "DC-API-SINGLE: Job %s: Failed to add output file \"%s\"",
				job->getId().c_str(), (*it).c_str());
			DC_destroyWU(wu);
			return false;
		}
		LOG(LOG_DEBUG, "DC-API-SINGLE: Output file \"%s\" added to WU of job \"%s\"",
			(*it).c_str(), job->getId().c_str());
	}

	if (DC_submitWU(wu))
	{
		LOG(LOG_ERR, "DC-API-SINGLE: Job %s: Failed to submit", job->getId().c_str());
		DC_destroyWU(wu);
		return false;
	}

	char *wu_id = DC_serializeWU(wu);
	job->setGridId(wu_id);
	job->setStatus(Job::RUNNING);

	LOG(LOG_INFO, "DC-API: WU %s: Submitted to grid %s (app '%s', job %s)",
		wu_id, job->getGrid().c_str(), algName, job->getId().c_str());

	free(wu_id);
	return true;
}

void DCAPIHandlerSingle::submitJobs(JobVector &jobs) throw (BackendException *)
{
	for (JobVector::iterator i = jobs.begin(); i != jobs.end(); i++)
	{
		if (!submit_job(*i))
			(*i)->setStatus(Job::ERROR);
	}
}


void DCAPIHandlerSingle::updateStatus(void) throw (BackendException *)
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
			LOG(LOG_DEBUG, "DC-API-SINGLE: WU %s: Cancelling", it->c_str());
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


GridHandler *DCAPIHandlerSingle::getInstance(GKeyFile *config, const char *instance)
{
	return new DCAPIHandlerSingle(config, instance);
}
