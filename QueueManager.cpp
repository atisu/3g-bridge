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

#include "Conf.h"
#include "DBHandler.h"
#include "Util.h"

#include <map>
#include <list>
#include <string>
#include <iostream>
#include <sstream>

#include <sys/stat.h>
#include <sys/time.h>
#include <sysexits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <gmodule.h>

using namespace std;

/**********************************************************************
 * Constants
 */

#define DEFAULT_SLEEP_INTERVAL	5
#define DEFAULT_UPDATE_INTERVAL	10

/**********************************************************************
 * Global variables
 */

/* If 'true', exit was requested by a signal */
static volatile bool finish;

/* If 'true', the log file should be re-opened */
static volatile bool reload;

/* The global configuration */
static GKeyFile *global_config = NULL;

/* Config: time to sleep when polling for new jobs */
static int sleep_interval;

/* Config: Location of the plugin directory */
static char *plugin_dir;

/* Config: min. time between calling the update method of plugins */
static int update_interval;

/* Command line: Location of the config file */
static char *config_file = (char *)SYSCONFDIR "/3g-bridge.conf";

/* Command line: If true, run as a daemon in the background */
static int run_as_daemon = 1;

/* Command line: If true, kill a running daemon */
static int kill_daemon;

/* Command line: Force debug mode */
static int debug_mode;

/* Command line: Print the version */
static int get_version;

/* Table of the command-line options */
static GOptionEntry options[] =
{
	{ "config",	'c',	0,			G_OPTION_ARG_FILENAME,	&config_file,
		"Configuration file to use", "FILE" },
	{ "nofork",	'f',	G_OPTION_FLAG_REVERSE,	G_OPTION_ARG_NONE,	&run_as_daemon,
		"Don't detach from the terminal and run in the foreground", NULL },
	{ "debug",	'd',	0,			G_OPTION_ARG_NONE,	&debug_mode,
		"Debug mode: don't fork, log to stdout", NULL },
	{ "kill",	'k',	0,			G_OPTION_ARG_NONE,	&kill_daemon,
		"Kill the running daemon", NULL },
	{ "version",		'V',	0,	G_OPTION_ARG_NONE,		&get_version,
		"Print the version and exit", NULL },
	{ NULL }
};

/* List of all configured grid handlers */
static vector<GridHandler *> gridHandlers;

/* Set of loaded plugins */
static GHashTable *modules;


/**********************************************************************
 * Prototypes
 */

static unsigned selectSizeAdv(AlgQueue *algQ);

/**********************************************************************
 * Misc. helper functions
 */

static void sigint_handler(int signal __attribute__((__unused__)))
{
	finish = true;
}

static void sighup_handler(int signal __attribute__((__unused__)))
{
	reload = true;
}

/**********************************************************************
 * Get a plugin instance by name
 */

static void close_module(void *ptr)
{
	g_module_close((GModule *)ptr);
}

/* Load the module and get retrieve the address of the factory symbol */
static handler_factory_func get_factory(const char *handler)
{
	handler_factory_func fn;
	GModule *module;

	module = (GModule *)g_hash_table_lookup(modules, handler);
	if (!module)
	{
		char *path = g_strdup_printf("%s/%s_handler.%s", plugin_dir, handler, G_MODULE_SUFFIX);
		module = g_module_open(path, G_MODULE_BIND_LOCAL);
		g_free(path);
		if (!module)
		{
			LOG(LOG_ERR, "Failed to load plugin %s_handler: %s", handler, g_module_error());
			return NULL;
		}
		g_hash_table_insert(modules, g_strdup(handler), module);
	}

	if (!g_module_symbol(module, G_STRINGIFY(HANDLER_FACTORY_SYMBOL), (void **)&fn))
	{
		LOG(LOG_ERR, "Failed to initialize plugin %s_handler: %s", handler, g_module_error());
		return NULL;
	}
	LOG(LOG_INFO, "Loaded plugin %s_handler", handler);
	return fn;
}

/**********************************************************************
 * Perform required actions for a grid handler
 */

static bool runHandler(GridHandler *handler)
{
	bool work_done = false;
	JobVector jobs;

	if (handler->schGroupByNames())
	{
		vector<AlgQueue *> algs;

		AlgQueue::getAlgs(algs, handler->getName());
		for (vector<AlgQueue *>::iterator it = algs.begin(); it != algs.end(); it++)
		{
			DBHandler *dbh = DBHandler::get();
			dbh->getJobs(jobs, handler->getName(), (*it)->getName(), Job::INIT, selectSizeAdv(*it));
			DBHandler::put(dbh);

			if (!jobs.empty())
			{
				handler->submitJobs(jobs);
				work_done = true;
				jobs.clear();
			}
		}
	}
	else
	{
		AlgQueue *alg = AlgQueue::getInstance(handler->getName());
		if (!alg) {
			LOG(LOG_WARNING, "Algorithm queue not found for plugin name \"%s\"!", handler->getName());
			return false;
		}

		DBHandler *dbh = DBHandler::get();
		dbh->getJobs(jobs, handler->getName(), Job::INIT, alg->getPackSize());
		DBHandler::put(dbh);

		if (!jobs.empty())
		{
			handler->submitJobs(jobs);
			work_done = true;
			jobs.clear();
		}
	}

	handler->checkUpdate(update_interval);

	return work_done;
}

/**
 * Main loop. Called periodically. Queries the database for new, sent, finished
 * and aborted jobs. Handler funcitons are called for the different job
 * vectors.
 */
static void do_mainloop()
{
	/* Call the runHandler() methods repeatedly until all of them says
	 * there is no more work to do */
	bool work_done;
	do
	{
		work_done = false;
		for (vector<GridHandler *>::const_iterator it = gridHandlers.begin(); it != gridHandlers.end(); it++)
			work_done |= runHandler(*it);
	} while (work_done && !finish);

	if (finish || reload || work_done)
		return;

	sleep(sleep_interval);
}


/**********************************************************************
 * Batch size calculation routines
 */


/**
 * Determine package size - advanced version. For CancerGrid, "optimal"
 * package sizes are counted here for the different algorithm queues. In
 * this version, the "goodness" of a package size reflects the "smallness"
 * of the package size's average turnaround time better.
 *
 * @param[in] algQ The algorithm queue to use. No other information is
 *                 needed, as the decision depends only on this
 * @return Determined package size
 */
static unsigned selectSizeAdv(AlgQueue *algQ)
{
	unsigned maxPSize = algQ->getPackSize();
	double tATT = 0;
	double pBorder[maxPSize];
	double vpBorder[maxPSize + 1];
	vector<processStatistics> *pStats = algQ->getPStats();

	for (unsigned i = 0; i < maxPSize; i++)
		tATT += (pStats->at(i).avgTT ? 1/pStats->at(i).avgTT : 1);

        pBorder[0] = (pStats->at(0).avgTT ? 1/pStats->at(0).avgTT : 1);
        vpBorder[0] = 0;
        for (unsigned i = 1; i < maxPSize; i++)
	{
                pBorder[i] = (pStats->at(i).avgTT ? 1/pStats->at(i).avgTT : 1);
                vpBorder[i] = vpBorder[i-1] + pBorder[i-1];
        }
        vpBorder[maxPSize] = tATT;

        double rand = (double)random() / RAND_MAX * tATT;
        for (unsigned i = 0; i < maxPSize; i++)
                if (vpBorder[i] <= rand && rand < vpBorder[i+1])
                    return i+1;

	return 1;
}

static void init_grid_handlers(void)
{
	char **sections;
	unsigned int i;

	sections = g_key_file_get_groups(global_config, NULL);
	for (i = 0; sections && sections[i]; i++)
	{
		char *handler;
		gboolean enabled;
		GError *err = NULL;

		handler = g_key_file_get_string(global_config, sections[i], "handler", NULL);
		enabled = g_key_file_get_boolean(global_config, sections[i], "enable", &err);
		if (err) /* It's enabled by default */
			enabled = TRUE;
		g_clear_error(&err);
		/* Skip sections that are not grid definitions or disabled */
		if (!handler ||
		    !enabled ||
		    g_key_file_get_boolean(global_config, sections[i], "disable", NULL)
		   )
			continue;

		handler_factory_func fn = get_factory(handler);
		if (!fn)
			continue;
		GridHandler *plugin = fn(global_config, sections[i]);
		if (!fn)
			continue;
		gridHandlers.push_back(plugin);
		LOG(LOG_DEBUG, "Initialized grid %s using %s", sections[i], handler);
		g_free(handler);
	}
	g_strfreev(sections);

	if (!gridHandlers.size())
	{
		LOG(LOG_NOTICE, "No grid handlers are defined in the config. file, exiting");
		exit(EX_OK);
	}
}

/**********************************************************************
 * The main program
 */

int main(int argc, char **argv)
{
	GOptionContext *context;
	struct sigaction sa;
	GError *error = NULL;

	/* Parse the command line */
	context = g_option_context_new("- queue manager for the 3G Bridge");
	g_option_context_add_main_entries(context, options, PACKAGE);
        if (!g_option_context_parse(context, &argc, &argv, &error))
	{
		LOG(LOG_ERR, "Failed to parse the command line options: %s", error->message);
		exit(EX_USAGE);
	}

	if (get_version)
	{
		cout << PACKAGE_STRING << endl;
		exit(EX_OK);
	}

	if (!config_file)
	{
		LOG(LOG_ERR, "The configuration file is not specified");
		exit(EX_USAGE);
	}
	g_option_context_free(context);

	global_config = g_key_file_new();
	g_key_file_load_from_file(global_config, config_file, G_KEY_FILE_NONE, &error);
	if (error)
	{
		LOG(LOG_ERR, "Failed to load the config file: %s", error->message);
		exit(EX_NOINPUT);
	}

	if (kill_daemon)
		exit(pid_file_kill(global_config, GROUP_BRIDGE));

	sleep_interval = g_key_file_get_integer(global_config, GROUP_BRIDGE,
		"sleep-interval", &error);
	if (error)
	{
		g_error_free(error);
		error = NULL;
		sleep_interval = DEFAULT_SLEEP_INTERVAL;
	}
	if (sleep_interval < 1)
		sleep_interval = DEFAULT_SLEEP_INTERVAL;

	update_interval = g_key_file_get_integer(global_config, GROUP_BRIDGE,
		"update-interval", &error);
	if (error)
	{
		g_error_free(error);
		error = NULL;
		update_interval = DEFAULT_UPDATE_INTERVAL;
	}
	if (update_interval < 1)
		update_interval = DEFAULT_UPDATE_INTERVAL;

	plugin_dir = g_key_file_get_string(global_config, GROUP_WSMONITOR, "plugin-dir", &error);
	if (!plugin_dir || error)
	{
		plugin_dir = g_strdup(PLUGINDIR);
		if (error)
		{
			g_error_free(error);
			error = NULL;
		}
	}

	if (debug_mode)
	{
		log_init_debug();
		run_as_daemon = 0;
	}
	else
		log_init(global_config, GROUP_BRIDGE);

	if (run_as_daemon && pid_file_create(global_config, GROUP_BRIDGE))
		exit(EX_OSERR);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	/* BOINC sends SIGHUP when it wants to kill the process */
	if (run_as_daemon)
		sa.sa_handler = sighup_handler;
	sigaction(SIGHUP, &sa, NULL);

	DBHandler::init(global_config);
	modules = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, close_module);

	try {
		init_grid_handlers();

		LOG(LOG_DEBUG, "Reading algorithm datas");
		AlgQueue::load();
	}
	catch (QMException *error) {
		LOG(LOG_ERR, "Fatal: %s", error->what());
		delete error;
		exit(EX_SOFTWARE);
	}

	if (run_as_daemon)
	{
		daemon(0, 0);
		pid_file_update();
	}

	while (!finish)
	{
		if (reload)
		{
			log_reopen();
			reload = false;
		}

		try {
			do_mainloop();
		}
		catch (QMException *e)
		{
			LOG(LOG_CRIT, "Fatal: %s", e->what());
			finish = true;
		}
		catch (...)
		{
			LOG(LOG_CRIT, "Fatal: Unknown exception caught");
			finish = true;
		}
	}

	LOG(LOG_NOTICE, "Exiting");

	while (!gridHandlers.empty())
	{
		GridHandler *plugin = gridHandlers.front();
		gridHandlers.erase(gridHandlers.begin());
		delete plugin;
	}

	AlgQueue::cleanUp();
	DBHandler::done();
	g_hash_table_destroy(modules);
	g_key_file_free(global_config);
	g_free(plugin_dir);

	return EX_OK;
}
