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

#ifndef GRIDHANDLER_H
#define GRIDHANDLER_H

#include "BackendException.h"
#include "Job.h"
#include "Plugin.h"

#include <glib.h>

#include <sys/time.h>
#include <vector>

using namespace std;

/**
 * GridHandler interface. Used by the QueueManager to submit jobs, update
 * status of jobs and get output of finished jobs using grid functions.
 */
class GridHandler : public Plugin {
public:
	/// Initialize GridHandler.
	GridHandler() {};

	/**
	 * Constructor using config file and instance name.
	 * @param config the config file object
	 * @param instance the name of the instance
	 * @see name
	 */
	GridHandler(GKeyFile *config, const char *instance):groupByNames(false), name(instance)
	{
		last_update.tv_sec = 0;
		last_update.tv_usec = 0;
	};

	/// Destructor
	virtual ~GridHandler() {}

	/**
	 * Return the name of the plugin instance.
	 * @see name
	 * @return instance name
	 */
	const char *getName(void) const { return name.c_str(); }

	/**
	 * Indicates wether the plugin groups jobs by algorithm names or not.
	 * @see groupByNames
	 * @return true if plugin groups algorithms by names
	 */
	bool schGroupByNames() const { return groupByNames; }

	/**
	 * Submit jobs in the argument.
	 * This function is responsible for submitting jobs stored in the jobs
	 * vector to the target grid middleware offered by the grid plugin
	 * instance. In case of an error, the implementation should throw a
	 * BackendException exception.
	 * @param jobs JobVector of jobs to be submitted
	 * @throws BackendException if any problem occured during submission
	 */
	virtual void submitJobs(JobVector &jobs) throw (BackendException *) = 0;

	/**
	 * Update the status of jobs.
	 * This function should update the status of each job belonging to the
	 * grid plugin instance. In case of an error a BackendException should
	 * be thrown.
	 * @throws BackendException if any problem occured during update
	 */
	virtual void updateStatus(void) throw (BackendException *) = 0;

	/**
	 * Poll the status of a submitted job.
	 * This function is used as a callback for the pollJobs function of
	 * DBHandler. Depending on the job's status the function should either
	 * update the job's status or cancel the job.
	 * @param job the job to poll
	 * @throws BackendException if any problem occured during update
	 */
	virtual void poll(Job *job) throw (BackendException *) = 0;

	/**
	 * Perform job status update if needed.
	 * This function is called by the QueueManager to perform job status
	 * update. If the last update has been performed more then interval
	 * seconds earlier, the updateStatus function is called to perform
	 * the update.
	 * @see last_update
	 * @param interval job status update interval in seconds
	 * @throws BackendException if any problem occured during update
	 */
	void checkUpdate(int interval) throw (BackendException *);

protected:
	/**
	 * Indicates wether the GridHandler plugin is able to create batch
	 * packages of job using the same executable or not. Should be true
	 * for plugins that are able to create one grid job out of a number
	 * of jobs in the database (e.g. DC-API).
	 */
	bool groupByNames;

	/// Instance name of the GridHandler plugin
	string name;

private:
	/// Time of last update
	struct timeval last_update;
};

#endif /* GRIDHANDLER_H */
