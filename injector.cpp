/*
 * Copyright (C) 2008 MTA SZTAKI LPDS
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

#include "DBHandler.h"
#include "QMException.h"

#include <iostream>

#include <uuid/uuid.h>
#include <sysexits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

using namespace std;

static char *config_file;
static char *name;
static char *grid;
static char **inputs;
static char **outputs;
static char *args;
static int get_version;

static GOptionEntry options[] =
{
	{ "config",		'c',	0,	G_OPTION_ARG_FILENAME,		&config_file,
		"Location of the config file", "FILE" },
	{ "algname",		'a',	0,	G_OPTION_ARG_STRING,		&name,
		"Name of the algorithm", "NAME" },
	{ "grid",		'g',	0,	G_OPTION_ARG_STRING,		&grid,
		"Name of the target grid", "NAME" },
	{ "input",		'i',	0,	G_OPTION_ARG_STRING_ARRAY,	&inputs,
		"Input file specifications", "NAME:PATH" },
	{ "output",		'o',	0,	G_OPTION_ARG_STRING_ARRAY,	&outputs,
		"Output file specifications", "NAME:PATH" },
	{ "params",		'p',	0,	G_OPTION_ARG_STRING,		&args,
		"Command-line parameters", "ARGS" },
	{ "version",		'V',	0,	G_OPTION_ARG_NONE,		&get_version,
		"Print the version and exit", NULL },
	{ NULL }
};

static GKeyFile *global_config = NULL;

int main(int argc, char **argv)
{
	GOptionContext *context;
	GError *error = NULL;

	/* Parse the command line */
	context = g_option_context_new("- Job injector");
	g_option_context_add_main_entries(context, options, PACKAGE);
        if (!g_option_context_parse(context, &argc, &argv, &error))
	{
		cerr <<  "Failed to parse the command line:" << error->message << endl;
		exit(EX_USAGE);
	}
	g_option_context_free(context);

	if (get_version)
	{
		cout << PACKAGE_STRING << endl;
		exit(EX_OK);
	}

	if (!config_file)
	{
		cerr << "The configuration file is not specified" << endl;
		exit(EX_USAGE);
	}

	global_config = g_key_file_new();
	error = NULL;
	g_key_file_load_from_file(global_config, config_file, G_KEY_FILE_NONE, &error);
	if (error)
	{
		cerr << "Failed to load the config file: " << error->message << endl;
		exit(EX_NOINPUT);
	}

	// Check for mandatory options
	if (!name)
	{
		cerr << "The algorithm name is missing" << endl;
		exit(EX_USAGE);
	}
	if (!grid)
	{
		cerr << "The destination grid name is missing" << endl;
		exit(EX_USAGE);
	}
	if (!inputs || !inputs[0])
	{
		cerr << "There are no input files specified" << endl;
		exit(EX_USAGE);
	}
	if (!outputs || !outputs[0])
	{
		cerr << "There are no output files specified" << endl;
		exit(EX_USAGE);
	}

	// Generate an identifier for our job
	uuid_t uuid;
	char jobID[37];
	uuid_generate(uuid);
	uuid_unparse(uuid, jobID);

	DBHandler::init(global_config);

	Job job(jobID, name, grid, args, Job::INIT);

	for (unsigned i = 0; inputs[i]; i++)
	{
		char *p = strchr(inputs[i], ':');
		if (!p)
		{
			cerr << "Invalid input specification: " << inputs[i] << endl;
			exit(EX_USAGE);
		}
		*p++ = '\0';
		job.addInput(inputs[i], p);
	}

	for (unsigned i = 0; outputs[i]; i++)
	{
		char *p = strchr(outputs[i], ':');
		if (!p)
		{
			cerr << "Invalid output specification: " << outputs[i] << endl;
			exit(EX_USAGE);
		}
		*p++ = '\0';
		job.addOutput(outputs[i], p);
	}

	try {
		DBHandler *dbh = DBHandler::get();
		dbh->addJob(job);
		DBHandler::put(dbh);
	}
	catch (QMException *e) {
		cerr << "Error: " << e->what() << endl;
		exit(EX_SOFTWARE);
	}

	cout << job.getId() << endl;

	return 0;
}
