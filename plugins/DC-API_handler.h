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

#ifndef DC_API_HANDLER_H
#define DC_API_HANDLER_H

#include "Job.h"
#include "GridHandler.h"

#include <vector>

using namespace std;

/**
 * DC-API grid plugin.
 * The DC-API grid plugin can be used to submit job batches to BOINC. A job
 * batch consists of a number of jobs belonging to the same application, using
 * different input/output files.
 */
class DCAPIHandler: public GridHandler {
public:
	/**
	 * Constructor.
	 * The constructor initializes DC-API.
	 * @param config configuration file object
	 * @param instance name of the plugin instance
	 * @throws BackendException
	 */
	DCAPIHandler(GKeyFile *config, const char *instance) throw (BackendException *);

	/// Empty destructor
	~DCAPIHandler() {};

	/**
	 * Submit jobs.
	 * This function is responsible for creating a workunit for the job
	 * instances stored in jobs. The function makes use of head, body and
	 * tail scripts for jobs (run before, at the time of, and after the
	 * job's execution) and pack and unpack scripts for creating the job
	 * batches and to unpack the results.
	 * @param jobs the jobs to submit in a batch
	 * @throws BackendException
	 */
	void submitJobs(JobVector &jobs) throw (BackendException *);

	/**
	 * Update status of jobs.
	 * As job status updates are performed using callbacks in DC-API, this
	 * function simply checks if all jobs within a batch have been
	 * cancelled, and if yes, cancels the workunit belonging to the batch.
	 * @throws BackendException
	 */
	void updateStatus(void) throw (BackendException *);

	/// Empty poll function.
	void poll(Job *job) throw (BackendException *) {}
};

#endif /* DC_API_HANDLER_H */
