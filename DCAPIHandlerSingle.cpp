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

#define SCRIPT_NAME		"script"
#define INPUT_DIR		"inputs"
#define INPUT_NAME		"inputs.pack"
#define OUTPUT_DIR		"outputs"
#define OUTPUT_NAME		"outputs.pack"
#define OUTPUT_PATTERN		"outputs/*"


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
		if (unlink(tmp))
		{
			LOG(LOG_ERR, "DC-API-SINGLE: WU %s: Failed to remove output file %s",
				wuid.c_str(), tmp);
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


void DCAPIHandlerSingle::submitJobs(JobVector &jobs) throw (BackendException *)
{
	JobVector::iterator i = jobs.begin();
	if (i == jobs.end())
		return;

	for (; i != jobs.end(); i++)
	{
		Job *job = *i;
		DC_Workunit *wu = 0;
		const char *algName = job->getAlgQueue()->getName().c_str();
		const char **cargs = NULL;

		vector<string> vargs;
		string args = job->getArgs();
    		size_t fpos = args.find_first_not_of(' ');
	        while (string::npos != fpos)
		{
			size_t npos = args.find_first_of(' ', fpos);
			vargs.push_back(args.substr(fpos, npos-fpos));
			fpos = args.find_first_not_of(' ', npos);
		}

		if (vargs.size())
		{
			int j = 0;
			cargs = new const char *[vargs.size()+1];
			for (vector<string>::iterator it = vargs.begin(); it != vargs.end(); it++, j++)
				cargs[j] = (*it).c_str();
			cargs[j] = NULL;
		}

		wu = DC_createWU(algName, cargs, 0, job->getId().c_str());
		if (!wu)
		{
			LOG(LOG_ERR, "DC-API-SINGLE: DC_createWU() failed for job \"%s\"", job->getId().c_str());
			delete[] cargs;
			continue;
		}
		delete[] cargs;

		auto_ptr< vector<string> > inputs = job->getInputs();
		for (vector<string>::iterator it = inputs->begin(); it != inputs->end(); it++)
		{
			int err = DC_addWUInput(wu, (*it).c_str(), job->getInputPath(*it).c_str(), DC_FILE_VOLATILE);
			if (err)
			{
				LOG(LOG_ERR, "DC-API-SINGLE: Failed to add input file \"%s\" to WU of job \"%s\"",
					job->getInputPath(*it).c_str(), job->getId().c_str());
				DC_destroyWU(wu);
				continue;
			}
			LOG(LOG_DEBUG, "DC-API-SINGLE: Input file \"%s\" added to WU of job \"%s\" as \"%s\"",
				job->getInputPath(*it).c_str(), job->getId().c_str(), (*it).c_str());
		}

		auto_ptr< vector<string> > outputs = job->getOutputs();
		for (vector<string>::iterator it = outputs->begin(); it != outputs->end(); it++)
		{
			int err = DC_addWUOutput(wu, (*it).c_str());
			if (err)
			{
				LOG(LOG_ERR, "DC-API-SINGLE: Failed to add output file \"%s\" to WU of job \"%s\"",
					(*it).c_str(), job->getId().c_str());
				DC_destroyWU(wu);
				continue;
			}
			LOG(LOG_DEBUG, "DC-API-SINGLE: Output file \"%s\" added to WU of job \"%s\"",
				(*it).c_str(), job->getId().c_str());
		}

		if (DC_submitWU(wu))
		{
			LOG(LOG_ERR, "DC-API-SINGLE: Failed to submit WU of job \"%s\"", job->getId().c_str());
			DC_destroyWU(wu);
		}

		char *wu_id = DC_serializeWU(wu);
		LOG(LOG_INFO, "DC-API: WU %s: Submitted to grid %s (app '%s')",
			wu_id, job->getGrid().c_str(), algName);

		job->setGridId(wu_id);
		job->setStatus(Job::RUNNING);
		free(wu_id);
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
