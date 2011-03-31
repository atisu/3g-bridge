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

#include "Conf.h"
#include "Job.h"
#include "Metajob_handler.h"
#include "MJ_parser.h"
#include "Util.h"
#include "DBHandler.h"

using namespace _3gbridgeParser;
using namespace std;

char const * const CONFIG_GROUP = "metajob";
char const * const CFG_MAXJOBS = "maxJobsAtOnce";

static char const DSEP = '|';

#define GETLINE() { if (!getline(is, token, DSEP))			\
			throw new BackendException("Invalid value in gridId field " \
						   " for meta-job: %s", \
						   jobId.c_str());	\
	}
#define RET_NUM(a) { sscanf(token.c_str(), "%lu", &(a)); }
#define RET_STR(a) { (a).swap(token); }

static void getExtraData(const string &data,
			 string &grid,
			 size_t &count, size_t &startLine,
			 string &reqd, string &succAt,
			 string const &jobId)
	throw (BackendException*)
{
	istringstream is(data);
	string token;

	GETLINE();
	RET_STR(grid);
	
	if (getline(is, token, DSEP))
	{
		RET_NUM(count);

		GETLINE();
		RET_NUM(startLine);

		GETLINE();
		RET_STR(reqd);

		GETLINE();
		RET_STR(succAt);
	}
	// else: leave default values
}

static void translateJob(Job const *job,
			 MetaJobDef &mjd, JobDef &jd,
			 string &mjfileName)
	throw (BackendException)
{
	size_t count = 0, startLine = 0;
	string reqd, succAt;
	string grid;
	getExtraData(job->getGridId(),
		     grid, count, startLine, reqd, succAt,
		     job->getId());

	mjd = MetaJobDef(count, startLine, reqd, succAt);
	jd = JobDef(grid, job->getName(), job->getArgs(),
		    *(job->getOutputs().get()),
		    job->getInputRefs());

	string mjfn = ""; //TODO: f(job->getId())
	mjfileName.swap(mjfn);
}

static void updateJob(Job *job, MetaJobDef const &mjd, JobDef const &jd)
	throw (BackendException)
{
	ostringstream os;
	os << jd.grid << DSEP << mjd.count << DSEP << mjd.startLine << DSEP;
	if (mjd.finished)
		os << mjd.required << DSEP << mjd.successAt;
	else
		os << mjd.strRequired << DSEP << mjd.strSuccessAt;
	job->setGridId(os.str());
	//TODO: inputs
}

MetajobHandler::MetajobHandler(GKeyFile *config, const char *instance)
	throw (BackendException*)
	: GridHandler(config, instance)
{
	groupByNames = false;

	GError *error = 0;
	maxJobsAtOnce = g_key_file_get_integer(config,
					       CONFIG_GROUP, CFG_MAXJOBS,
					       &error);
	if (error)
	{
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
			LOG(LOG_ERR,
			    "Failed to parse the max DB connection number: %s",
			    error->message);
		maxJobsAtOnce = 0; //unlimited
		g_error_free(error);
	}

	LOG(LOG_INFO, "Metajob Handler: instance '%s' initialized.",
	    name.c_str());
}

void MetajobHandler::submitJobs(JobVector &jobs)
	throw (BackendException *)
{
	for (JobVector::iterator i = jobs.begin(); i!= jobs.end(); i++)
	{
		MetaJobDef mjd;
		JobDef jd;
		string mjfileName;
		translateJob(*i, mjd, jd, mjfileName);

		ifstream mjfile(mjfileName.c_str());
		parseMetaJob(mjfile,
			     mjd, jd,
			     &qJobHandler,
			     maxJobsAtOnce);

		updateJob(*i, mjd, jd);

		if (mjd.finished) {
			//TODO:
			//Change sub-jobs' statuses to INIT
			//Change meta-job's status to RUNNING
		}
	}
}

void MetajobHandler::qJobHandler(_3gbridgeParser::JobDef const &jd, size_t count)
{
	//TODO: insert jobs with status = PREPARE
}

void MetajobHandler::updateStatus(void)
	throw (BackendException *)
{
	DBHandler *jobDB = DBHandler::get();
	jobDB->pollJobs(this, Job::RUNNING, Job::CANCEL);
	DBHandler::put(jobDB);
}

void MetajobHandler::poll(Job *job) throw (BackendException *)
{
	//TODO: aggregate sub-jobs' statuses
}

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new MetajobHandler(config, instance);
}

