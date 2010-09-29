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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>

#include "Util.h"
#include "BOINC_monitor.h"

/**********************************************************************
 * Plugin implementation
 */

BOINCMonitor::BOINCMonitor(GKeyFile *config, const char *instance) throw (BackendException *): MonitorHandler(config, instance)
{
	char *projectroot;

	projectroot = g_key_file_get_string(config, instance, "project-dir", NULL);
	g_strstrip(projectroot);
	boinc_config.parse_file(projectroot);
	g_free(projectroot);

	conn = mysql_init(0);
	if (!conn)
		throw new BackendException("Out of memory");
	if (!mysql_real_connect(conn, boinc_config.db_host, boinc_config.db_user,
			boinc_config.db_passwd, boinc_config.db_name, 0, 0, 0))
	{
		std::string error = mysql_error(conn);
		mysql_close(conn);
		conn = 0;
		throw new BackendException("Could not connect to the database: " + error);
	}

	last_job_query = 0;
	last_cpu_query = 0;

	in_progress = 0;
	unsent = 0;
	cpus = 0;
}

BOINCMonitor::~BOINCMonitor()
{
	mysql_close(conn);
}

void BOINCMonitor::query_jobs()
{
	time_t now = time(NULL);
	MYSQL_RES *res;
	MYSQL_ROW row;

	if (now - last_job_query < 600)
		return;

	const char *query = "SELECT "
		"COUNT(IF(server_state = 2, 1, NULL)) AS unsent, "
		"COUNT(IF(server_state = 4, 1, NULL)) AS in_progress "
		"FROM result";
	if (mysql_query(conn, query))
	{
		LOG(LOG_ERR, "Query %s has failed: %s", query, mysql_error(conn));
		return;
	}

	res = mysql_store_result(conn);
	if (!res)
	{
		LOG(LOG_ERR, "Failed to fetch results: %s", mysql_error(conn));
		return;
	}

	row = mysql_fetch_row(res);
	if (!row)
	{
		LOG(LOG_ERR, "Failed to fetch row: %s", mysql_error(conn));
		mysql_free_result(res);
		return;
	}

	unsent = strtoul(row[0], NULL, 10);
	in_progress = strtoul(row[1], NULL, 10);
	mysql_free_result(res);

	last_job_query = now;
}

unsigned BOINCMonitor::getRunningJobs() throw (BackendException *)
{
	query_jobs();
	return in_progress;
}

unsigned BOINCMonitor::getWaitingJobs() throw (BackendException *)
{
	query_jobs();
	return unsent;
}

unsigned BOINCMonitor::getCPUCount() throw (BackendException *)
{
	time_t now = time(NULL);

	if (now - last_cpu_query >= 600)
	{
		MYSQL_RES *res;
		MYSQL_ROW row;

		const char *query = "SELECT IFNULL(SUM(host.p_ncpus), 0) AS cpus "
			"FROM host "
			"WHERE host.rpc_time > UNIX_TIMESTAMP() - 24 * 60 * 60";
		if (mysql_query(conn, query))
		{
			LOG(LOG_ERR, "Query %s has failed: %s", query, mysql_error(conn));
			goto out;
		}

		res = mysql_store_result(conn);
		if (!res)
		{
			LOG(LOG_ERR, "Failed to fetch results: %s", mysql_error(conn));
			goto out;
		}

		row = mysql_fetch_row(res);
		if (!row)
		{
			LOG(LOG_ERR, "Failed to fetch row: %s", mysql_error(conn));
			mysql_free_result(res);
			goto out;
		}

		cpus = strtoul(row[0], NULL, 10);
		mysql_free_result(res);

		last_cpu_query = now;
	}

out:
	return cpus;
}

/**********************************************************************
 * Factory function
 */

MONITOR_FACTORY(config, instance)
{
	return new BOINCMonitor(config, instance);
}
