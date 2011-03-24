/*
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

#ifndef BOINC_MONITOR_H
#define BOINC_MONITOR_H

#include <time.h>

#include "MonitorHandler.h"

#include <boinc/sched_config.h>
#include <mysql.h>

/**
 * BOINC monitor.
 * The BOINC monitor can be used to monitor the status of a BOINC project.
 */
class BOINCMonitor: public MonitorHandler {
public:
	/**
	 * BOINCMonitor constructor.
	 * This constructor reads the config file, and connects to the database
	 * specified in the BOINC project's configuration file. Zeroes all
	 * statistics.
	 * @see last_job_query
	 * @see last_cpu_query
	 * @see in_progress
	 * @see unsent
	 * @see cpus
	 * @see conn
	 * @param config configuration file object
	 * @param instance name of monitor instance
	 * @throws BackendException
	 */
	BOINCMonitor(GKeyFile *config, const char *instance) throw (BackendException *);

	/**
	 * BOINCMonitor destructor.
	 * Closes connection to the BOINC project's MySQL server.
	 * @see conn
	 */
	~BOINCMonitor();

	/**
	 * Get number of running jobs.
	 * This function calls the query_jobs method and returns the number of
	 * running (in progress) jobs from the database. Those jobs (results)
	 * are considered as running for which the server_state field is 4 in
	 * the result table.
	 * @see in_progress
	 * @see query_jobs
	 * @return number of running jobs
	 * @throws BackendException
	 */
	unsigned getRunningJobs() throw (BackendException *);

	/**
	 * Get number of waiting jobs.
	 * This function calls the query_jobs method and returns the number of
	 * waiting (unsent) jobs from the database. Those jobs (results) are
	 * considered as waiting for which the server_state field is 1 in the
	 * result table.
	 * @see unsent
	 * @see query_jobs
	 * @return number of waiting jobs
	 * @throws BackendException
	 */
	unsigned getWaitingJobs() throw (BackendException *);

	/**
	 * Get number of CPUs.
	 * This function summarizes the number of CPU cores for those registered
	 * hosts in the database which have been active (have connected to the
	 * server for some reason) at most 24 hours prior to the query. This
	 * query is performed at most every 10 minutes.
	 * @see cpus
	 * @see last_cpu_query
	 * @return number of active CPU cores
	 * @throws BackendException
	 */
	unsigned getCPUCount() throw (BackendException *);

private:
	/// BOINC configuration
	SCHED_CONFIG boinc_config;

	/// MySQL connection
	MYSQL *conn;

	/**
	 * Query different job metrics.
	 * This function is used to query the number of running and waiting jobs
	 * from the BOINC database. The details of query are desribed with the
	 * getRunningJobs and getWaitingJobs methods. The query is performed at
	 * most every 10 minutes.
	 * @see unsent
	 * @see in_progress
	 * @see last_job_query
	 * @see getRunningJobs
	 * @see getWaitingJobs
	 */
	void query_jobs();

	/// Time of last job query
	time_t last_job_query;

	/// Number of running jobs
	unsigned in_progress;

	/// Number of waiting jobs
	unsigned unsent;

	/// Time of last number of CPUs query
	time_t last_cpu_query;

	/// Number of CPUs
	unsigned cpus;
};

#endif /* BOINC_MONITOR_H */
