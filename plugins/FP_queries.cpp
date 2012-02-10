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

#include "FP_queries.h"

namespace foreignprobe
{

MetricData* standardMetrics[] = {
	& m_running_wus,
	& m_waiting_workunits,
	& m_past_workunits,
	& m_past_workunits_24_hours,
	& m_success_rate,
	& m_cpus_available,
	& m_gflops,
	0
};

SimpleMetricData m_running_wus("running_workunits", "uint32", "wus",
			       "SELECT count(*) FROM result WHERE result.appid = %d AND result.server_state = 4");
SimpleMetricData m_waiting_workunits("waiting_workunits",
				     "uint32", "wus",
				     "SELECT count(*) FROM result WHERE result.appid = %d AND result.server_state = 2");
SimpleMetricData m_past_workunits("past_workunits", "uint32", "wus",
				  "SELECT count(*) FROM result WHERE result.appid = %d AND result.server_state = 5");
SimpleMetricData m_past_workunits_24_hours("past_workunits_24_hours", "uint32", "wus",
					   "SELECT count(*) FROM result WHERE result.appid = %d AND result.server_state = 5 AND UNIX_TIMESTAMP()-86400 < result.create_time;");
SimpleMetricData m_success_rate("success_rate", "float", "percentage",
				"SELECT IFNULL(COUNT(IF(result.outcome=1,1,NULL)) / COUNT(IF(result.outcome>0,1,NULL)),1.0) FROM result WHERE result.appid = %d;");
SimpleMetricData m_cpus_available("cpus_available", "uint32", "cpus",
				  "SELECT coalesce(SUM(host.p_ncpus)-COUNT(*), 0) FROM workunit, result, host WHERE result.workunitid = workunit.id AND host.id = result.hostid AND result.server_state = 4 AND host.rpc_time > UNIX_TIMESTAMP()-6*60*60");
SimpleMetricData m_gflops("gflops", "uint32", "gflops",
			  "SELECT coalesce(sum(host.p_fpops)/pow(10,9),0) FROM host WHERE host.rpc_time > UNIX_TIMESTAMP()-6*60*60;");

}
