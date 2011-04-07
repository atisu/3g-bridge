/* -*- mode: c++; coding: utf-8-unix -*-
 *
 * Copyright (C) 2009-2010 MTA SZTAKI LPDS
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

#include <sstream>
#include <cstdio>
#include <fstream>
#include <uuid/uuid.h>
#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include "Conf.h"
#include "Job.h"
#include "Metajob_handler.h"
#include "MJ_parser.h"
#include "Util.h"
#include "DLException.h"

//MJ Extra data information:
//  Extra information is stored in the job's gridid field.
//
//  TODO: Move to external table.
//
//  Stored as string, format:
//     grid{|count|startLine|required|successAt}
//  {} means optional, | is the '|' character itself
//
//  WSSubmitter stores only the grid. Everything else is added by updateJob()
//  (in this file).
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


using namespace _3gbridgeParser;
using namespace std;

#define DO_LOG_JOBS

CSTR_C CONFIG_GROUP = "MetaJob";
CSTR_C CFG_MAXJOBS = "maxJobsAtOnce";
CSTR_C CFG_MINEL = "minElapse";

/**
 * Cut out the base directory from the path of one of the meta-job's outputs.
 * This directory will be used as base directory for sub-job's outputs. */
static string getOutDirBase(const string &outPath);
/**
 * Construct and create the path to where output shall be saved. */
static string calc_output_path(const string &basedir,
			       const string &jobid,
			       const string &localName);
/**
 * Exception handler for submitJobs*/
static void submit_handleError(DBHandler *dbh, const char *msg, Job *job);

static size_t getConfInt(GKeyFile *config, CSTR key, int defVal)
{
	GError *error = 0;
	size_t value =
		g_key_file_get_integer(config, CONFIG_GROUP, key, &error);
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

MetajobHandler::MetajobHandler(GKeyFile *config, const char *instance)
	throw (BackendException*)
	: GridHandler(config, instance)
{
	groupByNames = false;

	maxJobsAtOnce = getConfInt(config, CFG_MAXJOBS, 0); //Default: unlimited
	minElapse = getConfInt(config, CFG_MINEL, 60); //Default: a minute

	LOG(LOG_INFO,
	    "Metajob Handler: instance '%s' initialized.",
	    name.c_str());
	LOG(LOG_INFO,
	    "MJ instance '%s': %s = %lu, %s = %lu",
	    name.c_str(), CFG_MAXJOBS, maxJobsAtOnce, CFG_MINEL, minElapse);
}

static void LOGJOB(const char * msg,
		   const MetaJobDef &mjd, const JobDef &jd);

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

		DBHWrapper dbh; // Gets a new dbhandler and implements finally
				// block to free it when dbh goes out of scope
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
			parseMetaJob(*dbh, mjfile, mjd, jd,
				     (queueJobHandler)&qJobHandler,
				     maxJobsAtOnce);

			LOGJOB("END STATE", mjd, jd);

			// Store state for next iteration
			updateJob(*dbh, job, mjd, jd);

			// Start sub-jobs if all are done
			if (mjd.finished) {
				LOG(LOG_INFO,
				    "Finished unfolding meta-job '%s'. "
				    "Total sub-jobs generated: %lu",
				    jId, mjd.count);
				LOG(LOG_INFO,
				    "Starting sub-jobs for meta-job '%s'.",
				    jId);

				dbh->setMetajobChildrenStatus(job->getId(),
							      Job::INIT);
				job->setStatus(Job::RUNNING);
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

void MetajobHandler::qJobHandler(DBHandler *instance,
				 _3gbridgeParser::JobDef const &jd,
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

		string base; // Output base dir; value will be set in first
			     // iteration
		// Copy outputs
		for (outputMap::const_iterator i = jd.outputs.begin();
		     i != jd.outputs.end(); i++)
		{
			if (base.empty())
				base = getOutDirBase(i->second);

			qmjob.addOutput(i->first,
					calc_output_path(base,
							 jobid,
							 i->first));
		}

		if (!instance->addJob(qmjob))
			throw BackendException(
				"DB error while inserting sub-job"
				"for meta-job '%s'.",
				jd.metajobid.c_str());
		qmjob.setMetajobId(jd.metajobid);
	}
}

void MetajobHandler::updateStatus(void)
	throw (BackendException *)
{
	DBHWrapper()->pollJobs(this, Job::RUNNING, Job::CANCEL);
}

void MetajobHandler::userCancel(Job *job)
{
	LOG(LOG_DEBUG, "Canceling job: '%s'", job->getId().c_str());
	//TODO
}
void MetajobHandler::finishedCancel(Job *job)
{
	//TODO
}
void MetajobHandler::errorCancel(Job *job)
{
	//TODO
}
void MetajobHandler::processOutputs(Job *job)
{
	//TODO
}

void MetajobHandler::deleteOutput(Job *job)
{
	//TODO
}

void MetajobHandler::poll(Job *job) throw (BackendException *)
{
	//TODO: aggregate sub-jobs' statuses

	const string &jid = job->getId();

	LOG(LOG_DEBUG, "Polling job status: '%s'", jid.c_str());

	DBHWrapper dbh;

	if (job->getStatus() == Job::CANCEL)
	{
		userCancel(job);
		return;
	}
	
	if (job->getStatus() != Job::RUNNING)
		throw BackendException("Job '%s' has unexpected status: %d",
				      jid.c_str(), job->getStatus());

	processOutputs(job);

	size_t count = 0, startLine = 0;
	string strReqd, strSuccAt;
	string grid;
	getExtraData(job->getGridId(),
		     grid, count, startLine, strReqd, strSuccAt,
		     job->getId());
	size_t required, succAt;
	if (!sscanf(strReqd.c_str(), "%lu", &required)
	    || !sscanf(strSuccAt.c_str(), "%lu", &succAt))
	{
		throw new BackendException(
			"Invalid data in meta-job information: "
			"required='%s', succAt='%s'; All should be numbers.",
			strReqd.c_str(), strSuccAt.c_str());
	}
	LOG(LOG_DEBUG,
	    "Job status for '%s'\n"
	    "  count = %lu\n"
	    "  required = %lu\n"
	    "  successAt = %lu\n", jid.c_str(), count, required, succAt);

	size_t all, err;
	dbh->getSubjobCounts(job->getId(), all, err);

	// Finished sub-jobs' output is processed, and then they are deleted.
	// So, histogram[FINISHED] is the number of jobs in FINISHED status,
	// with unprocessed outputs.
	// We only consider finished AND processed jobs as successfully
	// finished. They are the missing ones.
	size_t finished = count - all;

	if (finished > succAt)
	{
		finishedCancel(job);
	}
	else if (err > count - required)
	{
		// It is impossible to achieve the required number of successful
		// jobs. Cancel everything.
		errorCancel(job);
	}
	else if (finished + err == count)
	{
		// All jobs finished in either final state.
		if (finished > required)
			finishedCancel(job);
		else
			errorCancel(job);
	}
	

	// TODO:
	// get filename of sub-job status report
	// if file doesn't exist or it's older than minel,
	// -> regen sub-jobs' status info
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

void MetajobHandler::getExtraData(const string &data,
				  string &grid,
				  size_t &count, size_t &startLine,
				  string &reqd, string &succAt,
				  string const &jobId)
	throw (BackendException*)
{
	istringstream is(data);
	string token;

	//Info: search for "MJ Extra" (it's near the top of this file)
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

void MetajobHandler::updateJob(DBHandler *dbh,
			       Job *job,
			       MetaJobDef const &mjd,
			       JobDef const &jd)
	throw (BackendException*)
{
	//Info: search for "MJ Extra" (it's near the top of this file)
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

static string getOutDirBase(const string &outPath)
{
	// Output path format: ...base/jobid_prefix/jobid/filename
	// ==> base = dirname(dirname(dirname(path)))

	if (!outPath.empty())
	{
		CSTR_C start = outPath.c_str();
		CSTR i = start; // With this we will find the third to last
				// slash, which marks the end of the directory
				// name we're looking for

		// find last character
		while (*i) i++;
		i--;

		if (*i != '/') //else: not a filename; something is wrong
		{
			//(At most) three times dirname
			for (int j=0; j<3 && i >= start; j++)
			{
				//skip to next slash
				while (i >= start && *i != '/') i--;
				//skip this (and consecutive) slashes
				while (i >= start && *i == '/') i--;
			}

			// If there is still something left in the string, then
			// we found the base directory
			if (i >= start)
				return string(start, i-start+1);
		}
	}

	throw new BackendException(
		"Base path cannot be guessed from output path '%s'.",
		outPath.c_str());
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

static string calc_output_path(const string &basedir,
			       const string &jobid,
			       const string &localName)
{
	return make_hashed_dir(basedir, jobid) + '/' + localName;
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

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new MetajobHandler(config, instance);
}
