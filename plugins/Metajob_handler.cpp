/* -*- mode: c++; coding: utf-8-unix -*-
 *
 * Copyright (C) 2009-2010 MTA SZTAKI LPDS
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * In addition, as a special exception, MTA SZTAKI gives you permission to link
 * the code of its release of the 3G Bridge with the OpenSSL project's "OpenSSL"

 * library (or with modified versions of it that use the same license as the
 * "OpenSSL" library), and distribute the linked executables. You must obey the
 * GNU General Public License in all respects for all of the code used other
 * than "OpenSSL". If you modify this file, you may extend this exception to
 * your version of the file, but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version.
 */

#include <iostream>
#include <iomanip>
#include <cstdio>
#include <fstream>
#include <uuid/uuid.h>
#include <assert.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "Conf.h"
#include "Job.h"
#include "Metajob_handler.h"
#include "MJ_parser.h"
#include "Util.h"
#include "DLException.h"

using namespace _3gbridgeParser;
using namespace std;

typedef map<string, string> Outputmap;

//remove this to omit extensive logging during job generation
#define DO_LOG_JOBS

CSTR_C CFG_OUTDIR = "output-dir";
CSTR_C CFG_INDIR = "input-dir";

CSTR_C CFG_MAXJOBS = "maxJobsAtOnce";
CSTR_C CFG_MINEL = "minElapse";
CSTR_C CFG_OUTURL = "output-url-prefix";

CSTR_C MAP_SUFFIX = "-mapping.txt";
CSTR_C STATS_SUFFIX = "-stats.txt";

CSTR_C ARCH_RUNNING = ".arch.RUNNING";
CSTR_C ARCH_SUCCESS = ".arch.SUCCESS";
CSTR_C ARCH_ERROR   = ".arch.ERROR";
CSTR_C ARCH_STDERR  = ".arch.stderr";
CSTR_C ARCH_SCRIPT  = ".arch.create.sh";

/**
 * Construct and create the path to where output shall be saved. */
static string calc_job_path(const string &basedir,
			    const string &jobid);
/**
 * Construct and create the path to where output shall be saved. */
static string calc_file_path(const string &basedir,
			     const string &jobid,
			     const string &localName);
/**
 * Exception handler for submitJobs*/
static void submit_handleError(DBHandler *dbh, const char *msg, Job *job);

/** Recursive deletion of a directory */
static void rmrf(string dir);

/** Gets the given int value from the config. */
static size_t getConfInt(GKeyFile *config, CSTR group, CSTR key, int defVal);

/** Gets the given string value from the config. */
static string getConfStr(GKeyFile *config,
			 CSTR group, CSTR key,
			 CSTR defVal = 0);
/** Stores state information of parser to job griddata */
static void updateJobGenerationData(DBHandler *dbh, Job *job,
				    MetaJobDef const &mjd,
				    JobDef const &jd) throw (BackendException*);
/** Logs information about meta-job parser state. */
static void LOGJOB(const char * msg,
		   const MetaJobDef &mjd, const JobDef &jd);
/** Formats stat field with percentage info. */
static string pc(size_t part, size_t whole);

MetajobHandler::MetajobHandler(GKeyFile *config, const char *instance)
	throw (BackendException*)
	: GridHandler(config, instance)
{
	groupByNames = false;

	//Default: unlimited
	maxJobsAtOnce = getConfInt(config, name.c_str(), CFG_MAXJOBS, 0);
	//Default: a minute
	minElapse = getConfInt(config, name.c_str(), CFG_MINEL, 60);

	outDirBase = getConfStr(config, GROUP_WSSUBMITTER, CFG_OUTDIR);
	inDirBase = getConfStr(config, GROUP_WSSUBMITTER, CFG_INDIR);
	outURLBase = getConfStr(config, GROUP_WSSUBMITTER, CFG_OUTURL);

	LOG(LOG_INFO,
	    "Metajob Handler: instance '%s' initialized.",
	    name.c_str());
	LOG(LOG_INFO,
	    "MJ instance '%s': %s = %lu, %s = %lu",
	    name.c_str(), CFG_MAXJOBS, maxJobsAtOnce, CFG_MINEL, minElapse);
}

void MetajobHandler::submitJobs(JobVector &jobs)
	throw (BackendException *)
{
	for (JobVector::iterator i = jobs.begin(); i!= jobs.end(); i++)
	{
		Job *job = *i;
		CSTR_C jId = job->getId().c_str();

		LOG(LOG_DEBUG,
		    "MJ Unfolding job '%s'", jId);

		// Load last state (or initialize)
		// This will also check if _3gb-metajob is available on local
		// FS. If not, it will raise a DLException.
		MetaJobDef mjd;
		JobDef jd;
		string mjfileName;
		translateJob(job, mjd, jd, mjfileName);

		LOG(LOG_DEBUG,
		    "Will parse meta-job file: '%s'", mjfileName.c_str());
		LOGJOB("LOADED STATE", mjd, jd);

		// _3gb-metajob* on filesystem
		ifstream mjfile(mjfileName.c_str());

		ofstream f_mapping(
			calc_file_path(outDirBase,
				       job->getId(),
				       job->getId() + MAP_SUFFIX).c_str(),
			ios::app);
		mappingFile = &f_mapping;

		DBHWrapper dbh; // Gets a new dbhandler and implements finally
				// block to free it when dbh goes out of scope
		this->dbh = &dbh;
		//All operations in this block use the same dbh (and thus
		//transaction)
		try
		{
			LOG(LOG_INFO,
			    mjd.count
			    ? "Continuing to unfold meta-job: '%s'."
			    : "Starting to unfold meta-job: '%s'",
			    jId);

			// Unfolding a batch and storing the new state for the
			// next iteration is done in a single transaction. If
			// anything fails, the next iteration will start from a
			// consistent state, without generating duplicates.
			LOG(LOG_DEBUG, "Starting TRANSACTION");
			dbh->query("START TRANSACTION");

			// This will insert multiple jobs by calling qJobHandler
			LOG(LOG_DEBUG, "Starting parser...");
			parseMetaJob(this, mjfile, mjd, jd,
				     (queueJobHandler)&qJobHandler,
				     maxJobsAtOnce);

			LOGJOB("END STATE", mjd, jd);

			// Store state for next iteration
			updateJobGenerationData(*dbh, job, mjd, jd);

			// Start sub-jobs if all are done
			if (mjd.finished) {
				LOG(LOG_INFO,
				    "Finished unfolding meta-job '%s'. "
				    "Total sub-jobs generated: %lu",
				    jId, mjd.count);
				LOG(LOG_INFO,
				    "Starting sub-jobs for meta-job '%s'.",
				    jId);

				//Finish creating mapping file
				saveMJ(f_mapping, mjd);

				dbh->setMetajobChildrenStatus(
					job->getId(), Job::INIT);
				job->setStatus(Job::RUNNING);

				//Initially create stats file
				poll(job);
			}
			else
			{
				LOG(LOG_INFO,
				    "Sub-jobs generated so far "
				    "for meta-job '%s': %lu",
				    jId, mjd.count);
			}

			LOG(LOG_DEBUG, "Commiting TRANSACTION");
			dbh->query("COMMIT");
		}
		catch (const exception &ex)
		{
			// Rollback, logging, setting status, etc.
			// See just below.
			submit_handleError(*dbh,ex.what(),job);

			// Parser throws ParserException& but QueueManager only
			// handles QMException*.
			throw new BackendException("%s", ex.what());
		}
		catch (const QMException *ex)
		{
			submit_handleError(*dbh, ex->what(), job);
			throw;
		}
		catch (...)
		{
			submit_handleError(*dbh, "Unknown exception.", job);
			throw;
		}

		//dbh gets out of scope == finally { DBHandler::put(*dbh); }
	}
}
static void submit_handleError(DBHandler *dbh, const char *msg, Job *job)
{
	dbh->query("ROLLBACK");
	ostringstream out;
	out << "Error while unfolding meta-job '" << job->getId()
	    << "' : " << msg;
	const string &s_msg = out.str();
	LOG(LOG_ERR, "%s", s_msg.c_str());
	dbh->updateJobStat(job->getId(), Job::ERROR);
	job->setGridData(s_msg);
	dbh->removeMetajobChildren(job->getId());
}

void MetajobHandler::qJobHandler(MetajobHandler *instance,
				 _3gbridgeParser::JobDef &jd,
				 size_t count)
{
	for (size_t i = 0; i < count; i++) {
		//LATER: optimization: only one job object should be created,
		//only id would be changed in iterations

		uuid_t uuid;
		char jobid[37];

		uuid_generate(uuid);
		uuid_unparse(uuid, jobid);

		// Create job from template
		Job qmjob = Job(jobid,
				jd.algName.c_str(),
				jd.grid.c_str(),
				jd.args.c_str(),
				Job::PREPARE);
		//TODO: env?!

		// Copy inputs
		for (inputMap::const_iterator i = jd.inputs.begin();
		     i != jd.inputs.end(); i++)
			// Except _3gb-metajob*
			if (strncmp(i->first.c_str(),
				    _METAJOB_SPEC_PREFIX,
				    _METAJOB_SPEC_PREFIX_LEN)) // !startswith
			{
				qmjob.addInput(i->first, i->second);
			}

		// Copy outputs
		for (outputMap::const_iterator i = jd.outputs.begin();
		     i != jd.outputs.end(); i++)
		{
			qmjob.addOutput(i->first,
					calc_file_path(instance->outDirBase,
						       jobid,
						       i->first));
		}

		if (!(*(instance->dbh))->addJob(qmjob))
			throw BackendException(
				"DB error while inserting sub-job"
				"for meta-job '%s'.",
				jd.metajobid.c_str());
		qmjob.setMetajobId(jd.metajobid);

		jd.dbId = jobid;
		saveSJ(*(instance->mappingFile), jd);
	}
}

void MetajobHandler::updateStatus(void)
	throw (BackendException *)
{
	DBHWrapper()->pollJobs(this, Job::RUNNING, Job::CANCEL);
}

/********************************************
 * Updating meta-job status
 ********************************************/

void MetajobHandler::poll(Job *job) throw (BackendException *)
{
	const string &jid = job->getId();

	LOG(LOG_DEBUG, "Polling job status: '%s'", jid.c_str());

	// Cancel sub-jobs and delete meta-job if user has canceled
	if (job->getStatus() == Job::CANCEL)
	{
		userCancel(job);
		return;
	}
	
	if (job->getStatus() != Job::RUNNING)
		throw BackendException("Job '%s' has unexpected status: %d",
				      jid.c_str(), job->getStatus());

	DBHWrapper dbh;

	// Process the output of finished sub-jobs
	// Also, delete processed sub-jobs
	processFinishedSubjobsOutputs(dbh, job);

	// The new status is the function of these parameters:
	//  Total count, succAt, required, err
	size_t count,required, succAt, err, finished;
	getMetajobStatusInfo(job, dbh,
			     count, required, succAt, err, finished);

	bool archivingRunning;
	string errorMsg;
	bool metajobFinalized = updateJobStatus(job, dbh,
						count, required,
						succAt, err, finished,
						errorMsg, archivingRunning);
       
        updateStatsFileIfNeccesary(job, dbh,
				   count, required, succAt,
				   err, finished,
				   errorMsg, archivingRunning,
				   metajobFinalized);
}

void MetajobHandler::userCancel(Job *job)
{
	const string &jobid = job->getId();
	CSTR_C c_jobid = jobid.c_str();

	DBHWrapper dbh;

	size_t all, err;
	dbh->getSubjobCounts(jobid, all, err);

	string gid = job->getGridId();
	if (all == 0)
	{
		LOG(LOG_DEBUG,
		    "All sub-jobs has been canceled and deleted. "
		    "Deleting meta-job '%s'.", c_jobid);
		// Delete meta-job if there are no sub-jobs left.
		deleteJob(job);
	}
	else
	{
		// Wait for sub-jobs to be canceled

		// Sub-jobs must be canceled, but only once. GridId is used as a
		// flag. Empty gridId means the sub-jobs has been canceled
		// already.
		if (!gid.empty())
		{
			LOG(LOG_DEBUG, "Canceling job: '%s'.", c_jobid);

			//TODO: delete remaining output of sub-jobs
			dbh->cancelSubjobs(jobid);
			job->setGridId("");
		}
		else
		{
			LOG(LOG_DEBUG,
			    "Still waiting for sub-jobs to finish "
			    "(metajobid='%s').", c_jobid);
		}
	}
}
void MetajobHandler::finishedCancel(Job *job)
{
	LOG(LOG_DEBUG,
	    "Job '%s' is finished successfully, canceling remaining sub-jobs.",
	    job->getId().c_str());

	DBHWrapper()->cancelSubjobs(job->getId());
	
	startArchivingProcess(job);
}
void MetajobHandler::errorCancel(Job *job)
{
	DBHWrapper dbh;
	LOG(LOG_DEBUG,
	    "Job '%s' has FAILED, canceling remaining sub-jobs.",
	    job->getId().c_str());

	dbh->cancelSubjobs(job->getId());
	job->setStatus(Job::ERROR);
}

void MetajobHandler::getMetajobStatusInfo(const Job *job, DBHWrapper &dbh,
					  size_t &count,
					  size_t &required, size_t &succAt,
					  size_t &err, size_t &finished) const
{
	size_t startLine = 0; // unused, mandatory for getExtraData
	string grid;          // this too

	string strReqd, strSuccAt;
	getExtraData(job->getGridId(),
		     grid, count, startLine, strReqd, strSuccAt,
		     job->getId());

	// After parsing the meta-job, these have been set
	if (!sscanf(strReqd.c_str(), "%lu", &required)
	    || !sscanf(strSuccAt.c_str(), "%lu", &succAt))
	{
		throw new BackendException(
			"Invalid data in meta-job information: "
			"required='%s', succAt='%s'; All should be numbers.",
			strReqd.c_str(), strSuccAt.c_str());
	}
	// Get job-count from database
	size_t existing;
	dbh->getSubjobCounts(job->getId(), existing, err);

	// Finished sub-jobs' output is processed, and then they are deleted.
	// So, the number of jobs in FINISHED status, is the number of jobs with
	// unprocessed outputs.
	// We only consider finished AND processed jobs as successfully
	// finished. They are the missing ones.
	finished = count - existing;

	LOG(LOG_DEBUG,
	    "Job status for '%s'\n"
	    "  count     = %12lu\n"
	    "  required  = %12lu\n"
	    "  successAt = %12lu\n"
	    "  finished  = %12lu\n"
	    "  error     = %12lu\n"
	    "  existing  = %12lu\n",
	    job->getId().c_str(), count, required, succAt,
	    finished, err, existing);
}

bool MetajobHandler::updateJobStatus (
	Job *job, DBHWrapper &dbh,
	size_t count, size_t required, size_t succAt,
	size_t err, size_t finished,
	string &errorMsg, bool &archivingRunning)
{
	bool metajobFinalized = false;
	archivingRunning = isArchivingProcessRunning(job);

	if (isArchivingProcessFinished(job))
	{
		if (isArchivingSuccessful(job))
		{
			job->setStatus(Job::FINISHED);
			metajobFinalized = true;
		}
		else
		{
			errorMsg = archivingError(job);
			job->setStatus(Job::ERROR);
			metajobFinalized = true;
		}

		cleanupArchiving(job);
	}
	else if (archivingRunning)
	{
		LOG(LOG_DEBUG, "Still running archivation process for job '%s'",
		    job->getId().c_str());
	}
	else //Archiving hasn't even started yet
	{
		// Calculate new status for the meta-job
			
		if (finished >= succAt)
		{
			// Goal has been reached
			finishedCancel(job);
			metajobFinalized = true;
		}
		else if (err > count - required)
		{
			// It is impossible to achieve the required number of
			// successful jobs. Cancel everything.
			errorCancel(job);
			errorMsg = "Too many jobs failed.";
			metajobFinalized = true;
		}
		else if (finished + err == count)
		{
			// All jobs finished in either final state.
			if (finished >= required)
				finishedCancel(job);
			else
			{
				errorCancel(job);
				errorMsg = "Too many jobs failed.";
			}
			metajobFinalized = true;
		}
		else
			LOG(LOG_DEBUG, "Metajob '%s' still RUNNING.",
			    job->getId().c_str());
	}

	return metajobFinalized;
}

void MetajobHandler::updateStatsFileIfNeccesary (
	Job *job, DBHWrapper &dbh,
	size_t count, size_t required, size_t succAt,
	size_t err, size_t finished,
	const string &errorMsg, bool archivingRunning,
	bool metajobFinalized) const
{
	const string &jid = job->getId();

	// Update statistics if neccesary
	string statsFilename = jid + STATS_SUFFIX;
	string statsFile = calc_file_path(outDirBase, jid, statsFilename);

	ostringstream urlBaseS;
	urlBaseS << outURLBase << '/' << jid[0] << jid[1] << '/' << jid << '/';
	const string &urlBase = urlBaseS.str();

	time_t now = time(NULL);
	struct stat st;
	bool statsExists = !stat(statsFile.c_str(), &st);
	
	if (metajobFinalized // i.e. meta-job finished
	    || !statsExists
	    || (now - st.st_mtime) > (int)minElapse)
	{
		map<string, size_t> histo = dbh->getSubjobHisto(jid);
		ofstream stat(statsFile.c_str(), ios::trunc);

		size_t notStarted = histo["INIT"] + histo["PREPARE"];
		size_t running =
			histo["FINISHED"]+histo["RUNNING"]+histo["TEMPFAILED"];
		size_t stillNeed = succAt > finished ? succAt - finished : 0;

		stat << "# Stat generated at "<< asctime(localtime(&now))<< endl
		     << "Meta-job ID: " << jid << endl
		     << "Meta-job STATUS: " << statToStr(job->getStatus()) << endl;
		if (archivingRunning)
			stat << "  Packing sub-jobs' output..." << endl;
		if (!errorMsg.empty()) 
			stat << "Error message: " << errorMsg << endl;
		
		stat << endl
		     << "# Generation report" << endl
		     << "Total generated: " << pc(count, count) << endl
		     << "Required:        " << pc(required, count) << endl
		     << "Success at:      " << pc(succAt, count) << endl;
		if (metajobFinalized)
		{
		     stat << "Mapping: "
			  << urlBase << jid << MAP_SUFFIX << endl;
		}
		stat << endl
		     << "# Status report" << endl
		     << "Preparing:  " << pc(notStarted, count) << endl
		     << "Running:    " << pc(running, count) << endl
		     << "Error:      " << pc(err, count) << endl
		     << "Finished:   " << pc(finished, count) << endl
		     << "Still need: " << pc(stillNeed, count) << endl;

		job->setGridData(urlBase + statsFilename);
	}
}

void MetajobHandler::processFinishedSubjobsOutputs(DBHWrapper &dbh, Job *metajob)
{
	JobVector jobs;
	DBHWrapper()->getFinishedSubjobs(metajob->getId(), jobs, 0);
	for (JobVector::iterator i = jobs.begin(); i!=jobs.end(); i++)
	{
		Job *subjob = *i;
		
		const string &sjid = subjob->getId();
		const string &mjid = metajob->getId();
		LOG(LOG_DEBUG,
		    "Moving output of sub-job '%s' to meta-job output '%s'",
		    sjid.c_str(), mjid.c_str());
		
		ostringstream s_dstdir;
		s_dstdir << calc_job_path(outDirBase, metajob->getId()) 
			 << '/' << sjid[0] << sjid[1] << '/';
		const string &dstdir = s_dstdir.str();
		if (-1 == mkdir(dstdir.c_str(), 0750) && errno != EEXIST)
			throw BackendException("Error creating directory '%s'",
					       dstdir.c_str());
		
		const string &srcdir = calc_job_path(outDirBase, sjid);
		
		ostringstream command;
		command << "mv '" << srcdir << "' '" << dstdir << "'";
		const string &s_command = command.str();
		
		LOG(LOG_DEBUG, "Moving: '%s' -> '%s'",
		    srcdir.c_str(), dstdir.c_str());

		if (system(s_command.c_str()))
			LOG(LOG_DEBUG, "Error while executing command '%s'.",
			    s_command.c_str());
		
		deleteJob(subjob);
	}
}

void MetajobHandler::cleanupJob(Job *job)
{
	rmrf(calc_job_path(outDirBase, job->getId()));
	rmrf(calc_job_path(inDirBase, job->getId()));
}
void MetajobHandler::deleteJob(Job *job)
{
	cleanupJob(job);
	DBHWrapper()->deleteJob(job->getId());
}

/********************************************
 * Creating output archives
 ********************************************/

void MetajobHandler::startArchivingProcess(const Job *job)
{
	const string &scriptFileName = archFile(job, ARCH_SCRIPT);

	{
		ofstream runFile(archFile(job, ARCH_RUNNING).c_str());
		runFile << "" << endl;
	}

	const string &baseDir = calc_job_path(outDirBase, job->getId());
	
	{
		ofstream scriptFile(scriptFileName.c_str());
		
		bool first = true;
		scriptFile << "#!/bin/bash" << endl;
		for (Outputmap::const_iterator
			     i = job->getOutputMap().begin();
		     i != job->getOutputMap().end(); i++)
		{
			if (first) first = false;
			else scriptFile << " && ";

			scriptFile << "tar czf '" << i->first
				   << "' --remove-files $(find -name '"
				   << i->first << "') ";
		}
		if (!first)
		{
			scriptFile << " && touch '"
				   << archFile(job, ARCH_SUCCESS)
				   << "' || touch '"
				   << archFile(job, ARCH_ERROR) << "'";
		}
		scriptFile << endl;
	}

	ostringstream cmd;
	cmd << "cd '" << baseDir << "' && /bin/bash -r '" << scriptFileName
	    << "' 2> '" << archFile(job, ARCH_STDERR) << "'";
	const string &s_cmd = cmd.str();

	pid_t pid = fork();
	if (pid < 0)
		throw BackendException("Error executing fork. "
				       "Couldn't start archiving process "
				       "for job '%s'", job->getId().c_str());
	else if (pid == 0)
	{
		char bash[] = "/bin/bash";
		char arg0[] = "-c";
		char arg1[s_cmd.size()+1];
		strcpy(arg1, s_cmd.c_str());
		char * args[] = { bash, arg0, arg1, 0 };

		execv(bash, args);
	}
	else
	{
		LOG(LOG_DEBUG, "Spawned process '%d' to create archive for '%s'",
		    pid, job->getId().c_str());
	}
}

void MetajobHandler::cleanupArchiving(const Job *job)
{

#define AF(s) archFile(job, (ARCH_##s))
	string residue[] = { AF(SCRIPT), AF(SUCCESS), AF(ERROR),
			     AF(RUNNING), AF(STDERR) };
	for (int i=0; i<5; i++)
		remove(residue[i].c_str());

	ostringstream rmdirs;
	rmdirs << "find '" << calc_job_path(outDirBase, job->getId())
	       << "' -depth -type d -empty -exec rmdir {} \\;";
	const string &s_rmdirs = rmdirs.str();
	if (system(s_rmdirs.c_str()))
		LOG(LOG_DEBUG, "Failed to remove empty directories: '%s'",
		    s_rmdirs.c_str());
}
string MetajobHandler::archFile(const Job *job, CSTR_C basename) const
{
	// Currently: archFile is in outputDir and the filename does not contain
	// the job id. This can be changed, so these temporary files will not be
	// available through http.
	return calc_file_path(outDirBase, job->getId().c_str(), basename);
}
bool MetajobHandler::isArchivingProcessRunning(const Job *job) const
{
	return ifstream(archFile(job, ARCH_RUNNING).c_str());
}
bool MetajobHandler::isArchivingProcessFinished(const Job *job) const
{
	return ifstream(archFile(job, ARCH_SUCCESS).c_str())
		|| ifstream(archFile(job, ARCH_ERROR).c_str());
}
bool MetajobHandler::isArchivingSuccessful(const Job *job) const
{
	return ifstream(archFile(job, ARCH_SUCCESS).c_str());
}
string MetajobHandler::archivingError(const Job *job) const
{
	ifstream infile(archFile(job, ARCH_STDERR).c_str());
	if (infile)
	{
		string line;
		ostringstream msg;
		while (infile.good())
		{
			getline(infile, line);
			msg << line << endl;
		}
		return msg.str();
	}
	else
		return "";
}

/**********************************************************************
 * Utility functions
 */

static char const DSEP = '|';

CSTR_C MSG_INV_VAL_FMT = "Invalid value in gridId field for meta-job '%s': '%s'";
#define GET_TOKEN()							\
	{								\
		if (!getline(is, token, DSEP))				\
			throw new BackendException(MSG_INV_VAL_FMT,	\
						   jobId.c_str(),	\
						   data.c_str());	\
	}
#define RET_NUM(a)							\
	{								\
		if (1 != sscanf(token.c_str(), "%lu", &(a)))		\
			throw new BackendException(MSG_INV_VAL_FMT,	\
						   jobId.c_str(),	\
						   data.c_str());	\
	}
#define RET_STR(a) { (a).swap(token); }

//MJ Extra data information:
//  Extra information is stored in the job's gridid field.
//
//  TODO: Move to external table.
//
//  Stored as string, format:
//     grid{|count|startLine|required|successAt}
//  {} means optional, | is the '|' character itself
//
//  WSSubmitter stores only the grid. Everything else is added by
//  updateJobGenerationData() (in this file).
//
// grid:      Original destination grid; dest. grid for sub-jobs.
// count:     Number of sub-jobs generated so far.
// startLine: Last position in _3gb-metajob* sepcification. Continue
//            from here.
// required:  So many jobs needed to successfully finish in order for the
//            meta-job to be successful. It will only has meaning when all
//            sub-jobs are generated. At this time this will be an integer
//            defining the actual number. Up until that it will be a string
//            unparsed by parseMetajob().
// successAt: When this number of sub-jobs has successfully finished, all
//            remaining are canceled and the meta-job automatically
//            succeeds. Same deal as required.

void MetajobHandler::getExtraData(const string &data,
				  string &grid,
				  size_t &count, size_t &startLine,
				  string &reqd, string &succAt,
				  string const &jobId)
	throw (BackendException*)
{
	istringstream is(data);
	string token;

	//Info: search for "MJ Extra" above
	GET_TOKEN(); RET_STR(grid);

	// Rest is optional, but all-or-nothing
	if (getline(is, token, DSEP))
	{
		RET_NUM(count);

		GET_TOKEN(); RET_NUM(startLine);
		GET_TOKEN(); RET_STR(reqd);
		GET_TOKEN(); RET_STR(succAt);
	}
	// else: leave default values
}

void MetajobHandler::translateJob(Job const *job,
				  MetaJobDef &mjd,
				  JobDef &jd,
				  string &mjfileName)
	throw (BackendException*)
{
	size_t count = 0, startLine = 0;
	string reqd, succAt;
	string grid;
	getExtraData(job->getGridId(),
		     grid, count, startLine, reqd, succAt,
		     job->getId());

	mjd = MetaJobDef(count, startLine, reqd, succAt);
	jd = JobDef(job->getId(), grid, job->getName(), job->getArgs(),
		    job->getOutputMap(),
		    job->getInputRefs());

	bool foundMJSpec = false;
	DLException *ex = 0;
	bool mustThrowDLEx = false;
	// Find the path of the _3gb-metajob* file and download missing inputs
	for (inputMap::const_iterator i = jd.inputs.begin();
	     i != jd.inputs.end(); i++)
	{
		const string &mjfile = i->second.getURL();
		
		bool thisIsIt = !strncmp(i->first.c_str(),
					 _METAJOB_SPEC_PREFIX,
					 _METAJOB_SPEC_PREFIX_LEN);
		bool notDownloaded = '/' != mjfile[0];
	
		if (thisIsIt)
		{
			mjfileName = mjfile;
			foundMJSpec = true;
		}

		if (notDownloaded)
		{
			if (!ex)
				ex = new DLException(job->getId());
			ex->addInput(i->first);
		}

		if (thisIsIt && notDownloaded)
			mustThrowDLEx = true;
	}
	if (mustThrowDLEx) throw ex;

	if (!foundMJSpec)
		throw new BackendException(
			"Missing _3gb-metajob file for meta-job: '%s'",
			job->getId().c_str());
}

static void updateJobGenerationData(DBHandler *dbh,
				    Job *job,
				    MetaJobDef const &mjd,
				    JobDef const &jd)
	throw (BackendException*)
{
	//Info: search for "MJ Extra" above
	ostringstream os;
	os << jd.grid << DSEP << mjd.count << DSEP << mjd.startLine << DSEP;
	if (mjd.finished)
		os << mjd.required << DSEP << mjd.successAt;
	else
		os << mjd.strRequired << DSEP << mjd.strSuccessAt;
	job->setGridId(os.str());
	job->setArgs(jd.args);

	for (inputMap::const_iterator i = jd.inputs.begin();
	     i != jd.inputs.end(); i++)
	{
		// i->second here is a FileRef --> updating all fields
		dbh->updateInputPath(job->getId(), i->first, i->second);
	}
}

static void md(char const *name)
{
	int ret = mkdir(name, 0750);
	if (ret == -1 && errno != EEXIST)
		throw new QMException(
			"Failed to create directory '%s': %s",
			name, strerror(errno));
}

static string make_hashed_dir(const string &base,
			      const string &jobid,
			      bool create = true)
{
	string dir = base + '/' + jobid.at(0) + jobid.at(1);
	if (create)
		md(dir.c_str());

	dir += '/' + jobid;
	if (create)
		md(dir.c_str());

	return dir;
}
static string calc_job_path(const string &basedir, const string &jobid)
{
	return make_hashed_dir(basedir, jobid);
}
static string calc_file_path(const string &basedir,
			       const string &jobid,
			       const string &localName)
{
	return calc_job_path(basedir, jobid) + '/' + localName;
}

static void rmrf(string dir)
{
	//TODO: implement recursive deleting
	if (system(("rm -rf '" + dir + "'").c_str()))
		LOG(LOG_WARNING,
		    "Couldn't remove directory: '%s'",
		    dir.c_str());
}
static size_t getConfInt(GKeyFile *config, CSTR group, CSTR key, int defVal)
{
	GError *error = 0;
	size_t value =
		g_key_file_get_integer(config, group, key, &error);
	if (error)
	{
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
		{
			LOG(LOG_ERR,
			    "Metajob Handler: "
			    "Failed to parse configuration '%s': %s",
			    key, error->message);
		}
		value = defVal;
		g_error_free(error);
	}

	return value;
}
static string getConfStr(GKeyFile *config, CSTR group, CSTR key, CSTR defVal)
{
	GError *error = 0;
	gchar* value =
		g_key_file_get_string(config, group, key, &error);
	if (value)
		g_strstrip(value);
	if (error)
	{
		LOG(LOG_ERR,
		    "Metajob Handler: "
		    "Failed to parse configuration '%s': %s",
		    key, error->message);
		if (defVal)
			return string(defVal);
		else
			throw BackendException("Missing configuration: [%s]/%s",
					       group, key);
	}

	string s = value ? value : string();
	g_free(value);
	return s;
}

static void LOGJOB(const char * msg,
		   const MetaJobDef &mjd, const JobDef &jd)
{
#ifdef DO_LOG_JOBS
	LOG(LOG_DEBUG, "%s", msg);
	LOG(LOG_DEBUG, "Last state:");
	LOG(LOG_DEBUG, "  count        = %lu", mjd.count);
	LOG(LOG_DEBUG, "  startLine    = %lu", mjd.startLine);
	LOG(LOG_DEBUG, "  strRequired  = '%s'", mjd.strRequired.c_str());
	LOG(LOG_DEBUG, "  strSuccessAt = '%s'", mjd.strSuccessAt.c_str());
	LOG(LOG_DEBUG, "  required     = %lu", mjd.required);
	LOG(LOG_DEBUG, "  successAt    = %lu", mjd.successAt);
	LOG(LOG_DEBUG, "  finished     = %s", mjd.finished ? "true" : "false");
	LOG(LOG_DEBUG, "  ---");
	LOG(LOG_DEBUG, "  metajobid    = '%s'", jd.metajobid.c_str());
	LOG(LOG_DEBUG, "  dbId         = '%s'", jd.dbId.c_str());
	LOG(LOG_DEBUG, "  grid         = '%s'", jd.grid.c_str());
	LOG(LOG_DEBUG, "  algName      = '%s'", jd.algName.c_str());
	LOG(LOG_DEBUG, "  comment      = '%s'", jd.comment.c_str());
	LOG(LOG_DEBUG, "  args         = '%s'", jd.args.c_str());
	LOG(LOG_DEBUG, "  ---");
	LOG(LOG_DEBUG, "  <inputs>");
	for (inputMap::const_iterator i = jd.inputs.begin();
	     i != jd.inputs.end(); i++)
	{
		const FileRef &fr = i->second;
		LOG(LOG_DEBUG,
		    "    '%s' : '%s', '%s', %ld",
		    i->first.c_str(), fr.getURL().c_str(),
		    fr.getMD5(), fr.getSize());
	}
	LOG(LOG_DEBUG, "  <outputs>");
	for (outputMap::const_iterator i = jd.outputs.begin();
	     i != jd.outputs.end(); i++)
	{
		LOG(LOG_DEBUG,
		    "    '%s' : '%s'",
		    i->first.c_str(), i->second.c_str());
	}
	LOG(LOG_DEBUG, "// %s", msg);
#endif
}

static string pc(size_t part, size_t whole)
{
	ostringstream os;
	os << right << setfill(' ') << fixed;
	os << setw(8) << part << " ("
	   << setw(5) << setprecision(1)
	   << ((float)part)/whole * 100
	   << "%)";
	return os.str();
}

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new MetajobHandler(config, instance);
}
