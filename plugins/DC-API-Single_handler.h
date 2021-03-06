/* -*- mode: c++; coding: utf-8-unix -*-
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

#ifndef DC_API_SINGLE_HANDLER_H
#define DC_API_SINGLE_HANDLER_H

#include "Job.h"
#include "GridHandler.h"

#include <vector>

using namespace std;

/**
 * DC-API-Single handler.
 * Instances of this plugin can be used to submit jobs to BOINC. As opposed to
 * the DC-API plugin, the DC-API-Single plugin doesn't use batches of jobs for
 * creating workunits: each individual job will result in an individual workunit
 * once submitted.
 */
class DCAPISingleHandler: public GridHandler {
	string inDirBase, outDirBase;
	void cancelSubmittedWUs();
	void cancelUnsubmittedWUs();
	void rmrfFiles(const string &jobid);
public:
	/**
	 * Constructor of the DC-API-Single plugin.
	 * @param config configuration file object
	 * @param instance name of the plugin instance
	 * @throws BackendException
	 */
	DCAPISingleHandler(GKeyFile *config, const char *instance) throw (BackendException *);

	/// Empty destructor
	~DCAPISingleHandler() {};

	/**
	 * Submit jobs to BOINC.
	 * @param jobs vector of jobs to submit
	 * @throws BackendException
	 */
	void submitJobs(JobVector &jobs) throw (BackendException *);

	/**
	 * Update status of jobs.
	 * @throws BackendException
	 */
	void updateStatus(void) throw (BackendException *);

	/**
	 * Empty poll function.
	 * @param job the job to poll
	 * @throws BackendException
	 */
	void poll(Job *job) throw (BackendException *) {}
};

#endif /* DC_API_SINGLE_HANDLER_H */
