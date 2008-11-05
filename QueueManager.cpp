#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Conf.h"
#include "DBHandler.h"
#include "Util.h"

#ifdef HAVE_DCAPI
#include "DCAPIHandler.h"
#endif
#ifdef HAVE_EGEE
#include "EGEEHandler.h"
#endif

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

using namespace std;

/**********************************************************************
 * Type definitions
 */

typedef GridHandler *(*plugin_constructor)(GKeyFile *config, const char *instance);

/**********************************************************************
 * Global variables
 */

/* If 'true', exit was requested by a signal */
static volatile bool finish;

/* If 'true', the log file should be re-opened */
static volatile bool reload;

/* The global configuration */
static GKeyFile *global_config = NULL;

static GHashTable *plugins;

/* Command line: Location of the config file */
static char *config_file;

/* Command line: If true, run as a daemon in the background */
static int run_as_daemon = 1;

/* Command line: If true, kill a running daemon */
static int kill_daemon;

/* Command line: Force debug mode */
static int debug_mode;

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
	{ NULL }
};

/* List of all configured grid handlers */
static vector<GridHandler *> gridHandlers;


/**********************************************************************
 * Prototypes
 */

static unsigned selectSize(AlgQueue *algQ);
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

static void registerPlugin(const char *name, plugin_constructor ctor)
{
	if (!plugins)
		plugins = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
	g_hash_table_insert(plugins, (void *)name, (void *)ctor);
}

static GridHandler *getPluginInstance(GKeyFile *config, const char *plugin, const char *instance)
{
	if (!plugins)
		throw new QMException("Unknown grid plugin %s requested", plugin);
	plugin_constructor ctor = (plugin_constructor)g_hash_table_lookup(plugins, plugin);
	if (!ctor)
		throw new QMException("Unknown grid plugin %s requested", plugin);
	return ctor(config, instance);
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

	handler->updateStatus();

	return work_done;
}

/**
 * Main loop. Called periodically. Queries the database for new, sent, finished
 * and aborted jobs. Handler funcitons are called for the different job
 * vectors.
 */
static void do_mainloop()
{
	struct timeval begin, end, elapsed;

	/* Measure the time needed to maintain the database */
	gettimeofday(&begin, NULL);

	/* Call the runHandler() methods repeatedly until all of them says
	 * there is no more work to do */
	bool work_done;
	do
	{
		work_done = false;
		for (vector<GridHandler *>::const_iterator it = gridHandlers.begin(); it != gridHandlers.end(); it++)
			work_done |= runHandler(*it);
	} while (work_done && !finish);

	if (finish)
		return;

	gettimeofday(&end, NULL);
	timersub(&end, &begin, &elapsed);

	/* Sleep for 5x the time needed to run the body of this loop,
	 * but no more than 5 minutes and no less than 1 sec */
	elapsed.tv_sec *= 5;
	elapsed.tv_usec *= 5;
	elapsed.tv_sec += elapsed.tv_usec / 1000000;
	elapsed.tv_usec = elapsed.tv_usec % 1000000;

	if (elapsed.tv_sec > 300)
		elapsed.tv_sec = 300;
	else if (elapsed.tv_sec < 1)
		elapsed.tv_sec = 1;
	if (elapsed.tv_sec > 5)
		LOG(LOG_DEBUG, "Sleeping for %d seconds", (int)elapsed.tv_sec);
	sleep(elapsed.tv_sec);
}


/**********************************************************************
 * Batch size calculation routines
 */

/**
 * Determine package size. For CancerGrid, "optimal" package sizes are
 * counted here for the different algorithm queues.
 *
 * @param[in] algQ The algorithm queue to use. No other information is
 *                 needed, as the decision depends only on this
 * @return Determined package size
 */
#if 0
static unsigned selectSize(AlgQueue *algQ)
{
	unsigned maxPSize = algQ->getPackSize();
	double tATT = 0, tVB = 0;
	double vBorder[maxPSize];
	double pBorder[maxPSize];
	double vpBorder[maxPSize + 1];
	vector<processStatistics> *pStats = algQ->getPStats();

	for (unsigned i = 0; i < maxPSize; i++)
		tATT += pStats->at(i).avgTT;

	if (tATT)
    		for (unsigned i = 0; i < maxPSize; i++)
    		{
            		vBorder[i] = (double)1 - pStats->at(i).avgTT/tATT;
            		tVB += vBorder[i];
    		}
	else
	{
    		for (unsigned i = 0; i < maxPSize; i++)
			vBorder[i] = 1;
		tVB = maxPSize;
	}


        pBorder[0] = vBorder[0] / tVB;
        vpBorder[0] = 0;
        for (unsigned i = 1; i < maxPSize; i++)
	{
                pBorder[i] = vBorder[i] / tVB;
                vpBorder[i] = vpBorder[i-1] + pBorder[i-1];
        }
        vpBorder[maxPSize] = 1;

        double rand = (double)random() / RAND_MAX;
        for (unsigned i = 0; i < maxPSize; i++)
                if (vpBorder[i] <= rand && rand < vpBorder[i+1])
                    return i+1;

	return 1;
}
#endif


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
	char **sections, *handler;
	unsigned i;

#ifdef HAVE_DCAPI
	registerPlugin("DC-API", DCAPIHandler::getInstance);
#endif
#ifdef HAVE_EGEE
	registerPlugin("EGEE", EGEEHandler::getInstance);
#endif

	sections = g_key_file_get_groups(global_config, NULL);
	for (i = 0; sections && sections[i]; i++)
	{
		/* Skip sections that are not grid definitions */
		if (!strcmp(sections[i], GROUP_DEFAULTS) ||
				!strcmp(sections[i], GROUP_DATABASE) ||
				!strcmp(sections[i], GROUP_BRIDGE) ||
				!strcmp(sections[i], GROUP_WSSUBMITTER))
			continue;

		handler = g_key_file_get_string(global_config, sections[i], "handler", NULL);
		if (!handler)
			throw new QMException("Handler definition is missing in %s", sections[i]);

		GridHandler *plugin = getPluginInstance(global_config, handler, sections[i]);
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

		do_mainloop();
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
	g_key_file_free(global_config);

	return EX_OK;
}
