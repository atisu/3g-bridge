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

class BOINCMonitor: public MonitorHandler {
public:
	BOINCMonitor(GKeyFile *config, const char *instance) throw (BackendException *);
	~BOINCMonitor();

	unsigned getRunningJobs() throw (BackendException *);
	unsigned getWaitingJobs() throw (BackendException *);
	unsigned getCPUCount() throw (BackendException *);
private:
	SCHED_CONFIG boinc_config;

	/* We can't use boinc_db because that is a global variable */
	MYSQL *conn;

	void query_jobs();

	time_t last_job_query;
	unsigned in_progress, unsent;

	time_t last_cpu_query;
	unsigned cpus;
};

#endif /* BOINC_MONITOR_H */
