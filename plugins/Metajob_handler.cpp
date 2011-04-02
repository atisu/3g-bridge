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
#include "DBHandler.h"

using namespace _3gbridgeParser;
using namespace std;

char const * const CONFIG_GROUP = "metajob";
char const * const CFG_MAXJOBS = "maxJobsAtOnce";

static string getOutDirBase(const string &outPath);
static string calc_output_path(const string &basedir,
			       const string &jobid,
			       const string &localName);

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

		// Load last state (or initialize)
		MetaJobDef mjd;
		JobDef jd;
		string mjfileName;
		translateJob(job, mjd, jd, mjfileName);

		ifstream mjfile(mjfileName.c_str());

		assert(!commonDBH);
		DBHandler *dbh = getCommonDBH();
		try
		{
			dbh->query("START TRANSACTION");

			// This will insert multiple jobs by calling qJobHandler
			parseMetaJob(mjfile, mjd, jd,
				     &qJobHandler, maxJobsAtOnce);

			// Save state for next iteration
			updateJob(job, mjd, jd);

			// Start sub-jobs if all are done
			if (mjd.finished) {
				dbh->setMetajobChildrenStatus(job->getId(),
							      Job::INIT);
				job->setStatus(Job::RUNNING);
			}

			//TODO: log

			dbh->query("COMMIT");
			putCommonDBH();
		}
		catch (const ParserException &ex)
		{
			//TODO: log
			dbh->query("ROLLBACK");
			dbh->updateJobStat(job->getId(), Job::ERROR);
			//dbh->removeMetajobChildren(job->getId());
			putCommonDBH();
		}
	}
}

void MetajobHandler::qJobHandler(_3gbridgeParser::JobDef const &jd, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		//TODO: optimization: only one job object should be created,
		//only id would be changed in iterations

		uuid_t uuid;
		char jobid[37];

		uuid_generate(uuid);
		uuid_unparse(uuid, jobid);

		Job qmjob = Job(jobid,
				jd.metajobid.c_str(),
				jd.algName.c_str(),
				jd.grid.c_str(),
				jd.args.c_str(),
				Job::PREPARE);
		//TODO: env?!

		for (inputMap::const_iterator i = jd.inputs.begin();
		     i != jd.inputs.end(); i++)
		{
			qmjob.addInput(i->first, i->second);
		}

		string base;

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

		DBHandler *dbh = getCommonDBH();
		if (!dbh->addJob(qmjob))
			throw ParserException(
				-1, "DB error while inserting sub-job.");
	}
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

DBHandler *MetajobHandler::commonDBH = 0L;
DBHandler *MetajobHandler::getCommonDBH()
{
	if (commonDBH == 0L)
		commonDBH = DBHandler::get();
	return commonDBH;
}
void MetajobHandler::putCommonDBH()
{
	assert(commonDBH);
	DBHandler::put(commonDBH);
	commonDBH = 0L;
}

/**********************************************************************
 * Utility functions
 */

static char const DSEP = '|';

#define GETLINE() { if (!getline(is, token, DSEP))			\
			throw new BackendException("Invalid value in gridId field " \
						   " for meta-job: %s", \
						   jobId.c_str());	\
	}
#define RET_NUM(a) { sscanf(token.c_str(), "%lu", &(a)); }
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

	GETLINE(); RET_STR(grid);

	if (getline(is, token, DSEP)) //rest is optional: all-or-nothing
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

void MetajobHandler::translateJob(Job const *job,
				  MetaJobDef &mjd,
				  JobDef &jd,
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
	jd = JobDef(job->getId(), grid, job->getName(), job->getArgs(),
		    job->getOutputMap(),
		    job->getInputRefs());

	string mjfn = ""; //TODO: f(job->getId())
	mjfileName.swap(mjfn);
}

void MetajobHandler::updateJob(Job *job,
			       MetaJobDef const &mjd,
			       JobDef const &jd)
	throw (BackendException)
{
	ostringstream os;
	os << jd.grid << DSEP << mjd.count << DSEP << mjd.startLine << DSEP;
	if (mjd.finished)
		os << mjd.required << DSEP << mjd.successAt;
	else
		os << mjd.strRequired << DSEP << mjd.strSuccessAt;
	job->setGridId(os.str());

	DBHandler *dbh = getCommonDBH();
	for (inputMap::const_iterator i = jd.inputs.begin();
	     i != jd.inputs.end(); i++)
	{
		// Second here is a FileRef --> updating all fields
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
	return outPath; //TODO: metajob output path --> outdir base
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
