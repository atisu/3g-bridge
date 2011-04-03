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

CSTR_C CONFIG_GROUP = "metajob";
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
static void submit_handleError(DBHandler *dbh, const char *msg, const Job *job);

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
}

void MetajobHandler::submitJobs(JobVector &jobs)
	throw (BackendException *)
{
	for (JobVector::iterator i = jobs.begin(); i!= jobs.end(); i++)
	{
		Job *job = *i;
		CSTR_C jId = job->getId().c_str();

		// Load last state (or initialize)
		// This will also check if _3gb-metajob is available on local
		// FS. If not, it will raise a DLException.
		MetaJobDef mjd;
		JobDef jd;
		string mjfileName;
		translateJob(job, mjd, jd, mjfileName);

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
			dbh->query("START TRANSACTION");

			// This will insert multiple jobs by calling qJobHandler
			parseMetaJob(*dbh, mjfile, mjd, jd,
				     (queueJobHandler)&qJobHandler,
				     maxJobsAtOnce);

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
static void submit_handleError(DBHandler *dbh, const char *msg, const Job *job)
{
	dbh->query("ROLLBACK");
	LOG(LOG_ERR,
	    "Error while unfolding meta-job '%s': %s",
	    job->getId().c_str(), msg);
	dbh->updateJobStat(job->getId(), Job::ERROR);
	//TODO: Tell the user what happened ?
	//dbh->removeMetajobChildren(job->getId()); //TODO: ?
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
				jd.metajobid.c_str(),
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
	}
}

void MetajobHandler::updateStatus(void)
	throw (BackendException *)
{
	DBHWrapper()->pollJobs(this, Job::RUNNING, Job::CANCEL);
}

void MetajobHandler::poll(Job *job) throw (BackendException *)
{
	//TODO: aggregate sub-jobs' statuses

	DBHWrapper dbh;

	// if canceled -> cancel sub-jobs
	// // what happens when a job is canceled?  automatically deleted? if
	// // not, we must delete the subjobs

	// get histogram of sub-job statuses
	// calculate new status for meta-job
	// -> this is where required, succAt and count comes in

	// get filename of sub-job status report
	// if file doesn't exist or it's older than minel,
	// -> regen sub-jobs' status info
}

/**********************************************************************
 * Utility functions
 */

static char const DSEP = '|';

CSTR_C MSG_INV_VAL_FMT = "Invalid value in gridId field for meta-job: %s";
#define GET_TOKEN()							\
	{								\
		if (!getline(is, token, DSEP))				\
			throw new BackendException(MSG_INV_VAL_FMT,	\
						   jobId.c_str());	\
	}
#define RET_NUM(a)							\
	{								\
		if (1 != sscanf(token.c_str(), "%lu", &(a)))		\
			throw new BackendException(MSG_INV_VAL_FMT,	\
						   jobId.c_str());	\
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
	// Find the path of the _3gb-metajob* file
	for (inputMap::const_iterator i = jd.inputs.begin();
	     i != jd.inputs.end(); i++)
	{
		if (!strncmp(i->first.c_str(),
			     _METAJOB_SPEC_PREFIX,
			     _METAJOB_SPEC_PREFIX_LEN)) // startswith
		{
			const string &mjfile = i->second.getURL();
			if ('/' != mjfile[0])
			{
				DLException *ex = new DLException(job->getId());
				ex->addInput(i->first);
				throw ex;
			}
			else
			{
				mjfileName = mjfile;
				foundMJSpec = true;
				break;
			}
		}
	}

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

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new MetajobHandler(config, instance);
}
