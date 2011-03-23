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

#include <sysexits.h>

#include "soap/MonitorH.h"
#include "soap/Monitor.nsmap"

#include <glib.h>

using namespace std;

/**********************************************************************
 * Data type definitions
 */

/**
 * Query type enumeration.
 */
typedef enum
{
	Q_RUNNING = (1 << 0),
	Q_WAITING = (1 << 1),
	Q_CPUS = (1 << 2),
	Q_VERSION = (1 << 3),
	Q_ALL = Q_RUNNING | Q_WAITING | Q_CPUS,
} query_type;

/**
 * Query map.
 * A map from query name to query type enumeration.
 */
struct query_map
{
	/// Name of the query
	const char	*name;

	/// Assigned type of the query
	int		value;
};


/**********************************************************************
 * Prototypes
 */

/**********************************************************************
 * Global variables
 */

static char *endpoint;
static char *grid;
static char **query_list;
static int get_version;

static const GOptionEntry options[] =
{
	{ "endpoint",		'e',	0,	G_OPTION_ARG_STRING,		&endpoint,
		"Service endpoint", "URL" },
	{ "grid",		'g',	0,	G_OPTION_ARG_STRING,		&grid,
		"Name of the grid instance to query", "NAME" },
	{ "query",		'q',	0,	G_OPTION_ARG_STRING_ARRAY,	&query_list,
		"What kind of data to query", "NAME" },
	{ "version",		'V',	0,	G_OPTION_ARG_NONE,		&get_version,
		"Print the version and exit", NULL },
	{ NULL }
};

/* Map query string to type */
static const struct query_map query_map[] =
{
	{ "running", Q_RUNNING },
	{ "waiting", Q_WAITING },
	{ "cpus", Q_CPUS },
	{ "version", Q_VERSION },
	{ "all", Q_ALL },
};

/* Hack for gSoap */
struct Namespace namespaces[] = {{ NULL, }};

/**********************************************************************
 * Main program
 */

int main(int argc, char **argv)
{
	GOptionContext *context;
	GError *error = NULL;
	struct soap *soap;
	unsigned j, resp;
	int what, i;

	context = g_option_context_new("- Simple client for the 3G Monitor service");
	g_option_context_add_main_entries(context, options, PACKAGE);

        if (!g_option_context_parse(context, &argc, &argv, &error))
	{
		cerr << "Error: " << error->message << endl;
		exit(EX_USAGE);
	}

	if (get_version)
	{
		cout << PACKAGE_STRING << endl;
		exit(EX_OK);
	}

	if (!endpoint)
	{
		cerr << "Error: no endpoint URL specified!" << endl;
		exit(EX_USAGE);
	}

	what = 0;
	if (!query_list || !query_list[0])
		what = Q_ALL;

	for (i = 0; query_list && query_list[i]; i++)
	{
		int value = 0;

		for (j = 0; j < G_N_ELEMENTS(query_map); j++)
			if (!strcmp(query_list[i], query_map[j].name))
				value = query_map[j].value;
		if (!value)
		{
			cerr << "Error: unknown query type " << query_list[i] << endl;
			exit(EX_USAGE);
		}

		what |= value;
	}

	if (what != Q_VERSION && !grid)
	{
		cerr << "Error: the grid name is not specified!" << endl;
		exit(EX_USAGE);
	}

	soap = soap_new();
	soap_set_namespaces(soap, Monitor_namespaces);

	if (what & Q_RUNNING)
	{
		if (SOAP_OK != soap_call___G3BridgeMonitor__getRunningJobs(soap, endpoint, NULL, grid, resp))
		{
			soap_print_fault(soap, stderr);
			exit(EX_UNAVAILABLE);
		}
		printf("running_jobs: %u\n", resp);
	}
	if (what & Q_WAITING)
	{
		if (SOAP_OK != soap_call___G3BridgeMonitor__getWaitingJobs(soap, endpoint, NULL, grid, resp))
		{
			soap_print_fault(soap, stderr);
			exit(EX_UNAVAILABLE);
		}
		printf("waiting_jobs: %u\n", resp);
	}
	if (what & Q_CPUS)
	{
		if (SOAP_OK != soap_call___G3BridgeMonitor__getCPUCount(soap, endpoint, NULL, grid, resp))
		{
			soap_print_fault(soap, stderr);
			exit(EX_UNAVAILABLE);
		}
		printf("cpu_count: %u\n", resp);
	}

	if (what & Q_VERSION)
	{
		std::string ver;

		if (SOAP_OK != soap_call___G3BridgeMonitor__getVersion(soap, endpoint, NULL, ver))
		{
			soap_print_fault(soap, stderr);
			exit(EX_UNAVAILABLE);
		}
		printf("version: %s\n", ver.c_str());
	}

	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);

	return EX_OK;
}
