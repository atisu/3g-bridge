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

#ifndef METAJOB_HANDLER_H
#define METAJOB_HANDLER_H

#include "Job.h"
#include "GridHandler.h"
#include "MJ_parser.h"
#include "DBHandler.h"

using namespace std;
using namespace _3gbridgeParser;

/**
 * TODO: comment
 */
class MetajobHandler : public GridHandler
{
public:
	/**
	 * Creates a MetajobHandler instance.
	 * @param config configuration
	 * @param instance TODO: ???
	 */
	MetajobHandler(GKeyFile *config, const char *instance)
		throw (BackendException *);
	~MetajobHandler() throw () {}

	/**
	 * Submit jobs. Submits a vector of jobs to EGEE.
	 * @param jobs jobs to submit
	 */
	void submitJobs(JobVector &jobs) throw (BackendException *);

	/**
	 * Update status of jobs. Updates status of jobs belonging to the
	 * plugin instance.
	 */
	void updateStatus(void) throw (BackendException *);

	/**
	 * Handle a given job. DBHandler uses this function to perform
	 * different operations on a job.
	 * @param job the job to handle
	 */
	void poll(Job *job) throw (BackendException *);

private:
	/**
	 * Delegate function. Called by parseMetaJob() to create jobs in the
	 * database using jd as a template.
	 * @see queueJobHandler
	 */
	static void qJobHandler(DBHandler *instance,
				_3gbridgeParser::JobDef const &jd,
				size_t count);
	//
	// Utility functions
	//

	/**
	 * Store parser state back to DB */
	static void updateJob(DBHandler *dbh,
			      Job *job,
			      MetaJobDef const &mjd,
			      JobDef const &jd)
		throw (BackendException*);
	/**
	 * Create input for parseMetaJob() from a Job object. */
	static void translateJob(Job const *job, MetaJobDef &mjd, JobDef &jd,
				 string &mjfileName)
		throw (BackendException*);
	/**
	 * Parse data stored in gridId of meta-job. */
	static void getExtraData(const string &data,
				 string &grid,  size_t &count, size_t &startLine,
				 string &reqd, string &succAt, string const &jobId)
		throw (BackendException*);

	/**
	 * From configuration: Max number of jobs to generate in an
	 * iteration. */
	size_t maxJobsAtOnce;
	/**
	 * From configuration: Minimum time (in seconds) to elapse between
	 * updates sub-jobs' status report */
	size_t minElapse;
};


#endif //METAJOB_HANDLER_H
