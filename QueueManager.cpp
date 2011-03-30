/*
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

/**
 * @mainpage 3G Bridge
 *
 *
 * @section About About
 * 3G Bridge is the Generic Grid-Grid Bridge used as a core component in
 * a number of grid interoperability solutions. It consists of two main modules:
 * the 3G Bridge service itself and the web service interface called
 * WSSubmitter. The internal data representation is making use of MySQL in order
 * to offer persistance.
 *
 * The following image shows the internal architecture of 3G Bridge with the main
 * components:
 * @image html doc/Internal_Architecture.png "3G Bridge's internal architecture"
 *
 * The first block shows the monitoring components with its web service
 * interface.
 *
 * The second block shows the components offering web service interface for job
 * management in 3G Bridge.
 *
 * Finally, the third block shows core 3G Bridge services responsible for
 * actually performing the job management in destination grids.
 *
 *
 * @section idr Internal data representation
 * Data of 3G Bridge is stored in a MySQL database. The important tables are
 * cg_job, cg_inputs, cg_outputs, cg_algqueue, cg_download and cg_env. cg_job
 * stores main properties of jobs, cg_algqueue stores information about plugins,
 * cg_inputs stores references of jobs' input files, cg_output stores output
 * file information, cg_download contains file entries to download, and cg_env
 * stores environment variables of jobs.
 *
 * @see DBHandler
 * @see DBResult
 * @see Job
 * @see AlgQueue
 *
 *
 * @section whatver 3G Bridge service
 * The core 3G Bridge service is responsible for performing management of jobs
 * using the grid plugins. The base of the 3G Bridge service is the Queue
 * Manager component that periodically calls the different grid plugins to
 * perform something useful. The grid plugins implement the GridHandler
 * interface in order to hide the details of grid systems. Thus, Queue Manager
 * itself doesn't contain any grid-specific code.
 *
 * @see QueueManager
 * @see QMException
 * @see DLException
 *
 *
 * @section wsss WSSubmitter service
 * WSSubmitter implements a web service interface above 3G Bridge, based on a
 * very simple WSDL, using gSOAP. The web service interface offers job
 * submission, status queries, output file listing and job cancel/removal. A
 * typical job management scenarion from the client's side is the following:
 * \li sumbit the job
 * \li query status of job as long as the job's status isn't FINISHED
 * \li get list of output files
 * \li download output files
 * \li remove the job
 *
 * 3G Bridge offers a client called \c wsclient, but it is possible to create
 * any SOAP client based on the WSDL (for example Java client using Axis).
 *
 * WSSubmitter also offers the DownloadManager component that is able to fetch
 * jobs' input files. Initially, every input file URL submitted through the web
 * service interface is accepted. Once 3G Bridge invokes the grid plugins to
 * submit the jobs, the grid plugins may examine the input URLs and throw a
 * properly initialized DLException exception if a given URL cannot be handled
 * by the grid plugin. In such cases the Queue Manager catches the exception,
 * and fills in the cg_download table according to the exception's contents, and
 * instructs the WSSubmitter to reread the file download table in order to
 * download the problematic files. Once all problematic files have been fetched,
 * the given jobs are submitted by the relevant plugin.
 *
 * @see WSSubmitter
 * @see DBHandler
 * @see DownloadManager
 * @see DLException
 *
 *
 * @section dp Destination plugins
 * Destination plugins implementing the GridHandler interface are used to
 * interact with destination grids.
 * Each plugin has to offer the following functionalities:
 * \li submit a JobVector: this functionality has to be implemented in the
 * \c submitJobs() method. The outcome of this operation has to be that the
 * grid identifier of submitted jobs is set, and status of submitted jobs has to
 * be RUNNING. If the grid plugin is unable to handle some input file URLs, a
 * properly set up DLException exception has to be thrown. The \c submitJobs()
 * method will receive jobs in the INIT status.
 * \li update jobs's status: this functionality is performed with the help of
 * the \c updateStatus() and \c poll() functions, and the DBHandler component.
 * The \c updateStatus() function should call the DBHandler::pollJobs()
 * function to invoke the grid plugin's \c poll() method for the jobs the grid
 * plugin is interested in (typicall jobs in the RUNNING and CANCEL status). The
 * \c poll() function of the grid plugin is reponsible for actually performing
 * the status update.
 *
 * @see Job
 * @see JobVector
 * @see DBHandler
 *
 *
 * @subsection bp BOINC-related plugins
 * 3G Bridge currently offers two grid plugins for submitting jobs to BOINC:
 * DC-API and DC-API-Single. Both of these plugins make use of DC-API
 * (http://www.desktopgrid.hu/storage/dcdoc/index.html) to interact with BOINC.
 *
 *
 * @subsubsection dcp DC-API plugin
 * The DC-API plugin (implemented in DCAPIHandler) can be used to submit jobs
 * to BOINC as a batch: a limited number (batch size) of jobs are packed
 * together into a BOINC workunit. Such workunits are submitted as follows:
 * \li a batch pack script (specified in DC-API configuration files) is used to
 * pack together input files of jobs in a batch, resulting in an input package
 * \li a batch script is created using a sequence of instantiated versions of a
 * job template file
 * \li a batch has one output package file
 * \li the workunit is submitted using the input package and batch script as the
 * input files, and the output package as the output files.
 *
 * The execution of such batches is as follows:
 * \li the batch script's head (a script excerpt specified in the DC-API
 * configuration file) is used to unpack the input package
 * \li subsequent parts of the batch script (instantiated version of the job
 * template) execute jobs in the batch
 * \li the batch script's tail (a script excerpt specified in the DC-API
 * configuration file) is used to create the output package file based on the
 * participating jobs' results.
 *
 * Finally, if a batch has been processed, the DC-API plugin uses the batch
 * unpack script to unpack the output package of the batch in order to collect
 * results of the individual jobs in the batch.
 *
 * @see GridHandler
 * @see Job
 *
 *
 * @subsubsection dcsp DC-API-Single plugin
 * The DC-API-Single plugin (implemented in DCAPISingleHandler) is a simplified
 * version of the DC-API plugin: it doesn't put a number of jobs in a workunit,
 * but handles the different 3G Bridge jobs individually, thus each workunit
 * contains only one job.
 *
 * @see GridHandler
 * @see Job
 *
 *
 * @subsection egee EGEE (gLite) plugin
 * The EGEE plugin (implemented in EGEEHandler) can be used to submit jobs to
 * gLite. One instance of the EGEE plugin is capable to submit to a specific VO
 * using a specific proxy, which is configured with the plugin instance, and is
 * aquired from a MyProxy server as needed. Management of gLite jobs is achieved
 * by using gLite and Globus API functions.
 *
 * @see GridHandler
 * @see Job
 *
 *
 * @subsection xweb XtremWeb plugin
 * The XtremWeb plugin (implemented in XWHandler) can be used to submit jobs to
 * XtremWeb.
 *
 * @see GridHandler
 * @see Job
 *
 *
 * @subsection nullp Null plugin
 * The Null plugin (implemented in NullHandler) is an example plugin
 * implementation that simply sets jobs' status to RUNNING during the
 * submission, and sets jobs's status to FINISHED during status update.
 * 
 * @see GridHandler
 * @see Job
 *
 *
 * @subsection ec2p EC2 plugin
 * The EC2 plugin (implemented in EC2Handler) can be used to submit jobs to
 * Amazon EC2 clouds.
 *
 * @see GridHandler
 * @see Job
 *
 *
 * @subsection javap Java plugin and derivatives
 * The Java plugin (implemented in JavaHandler) can be used to connect plugins
 * to the 3G Bridge implemented in Java through JNI. Java plugins have to
 * extend the hu.sztaki.lpds.G3Bridge.GridHandler class. Currently, one Java
 * plugin is offered for BES-compatible resources.
 *
 * @see hu.sztaki.lpds.G3Bridge.GridHandler
 *
 *
 * @subsubsection besp BES plugin
 * The BES Java plugin (implemented in BESHandler) can be used to submit jobs
 * to OGSA BES-capable resources.
 *
 * @see hu.sztaki.lpds.G3Bridge.GridHandler
 *
 *
 * @section mons Monitor service
 * The Monitor service of 3G Bridge is an extension that allows to query the
 * the basic status of grids supported by the different grid plugins. The
 * monitoring component is separate from the 3G Bridge just like the web service
 * interface: it can be accessed as a web service called WSMonitor. Grid monitor
 * plugins have to implement the MonitorHandler interface in order to report the
 * followings about a grid: number of running jobs, number of waiting jobs and
 * number of CPU cores.
 *
 * Beside the web service, a client is also offered by 3G Bridge called
 * wsmonitorclient. Although any entity may create its own client based on the
 * Monitor service's WSDL.
 *
 * Currently, an example Null monitor and a BOINC monitor is offered by 3G
 * Bridge.
 *
 *
 * @subsection nullm Null monitor
 * The Null monitor (implemented in NullMonitor) reports each metric as 0.
 *
 * @see MonitorHandler
 *
 *
 * @subsection boincm BOINC monitor
 * The BOINC monitor (implemented in BOINCMonitor) can be used to get some
 * information about a BOINC project:
 * \li number of running jobs: reports the number of workunits in the running
 * status,
 * \li number of waiting jobs: reports the number of workunits waiting to be
 * sent to clients,
 * \li number of CPU cores: reports the sum of CPU cores belonging to hosts
 * that were active in the last 24 hours.
 *
 *
 * @section Configuration
 *
 * Configuration of 3G Bridge components (core 3G Bridge, WSSubmitter and
 * WSMonitor) is performed through a single file called 3g-bridge.conf. Please
 * consult the file's man page 3g-bridge.conf(5) for details on how to configure
 * the different components and the different grid plugins.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Conf.h"
#include "DBHandler.h"
#include "DLException.h"
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
#include <errno.h>

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

/* Config: DB reread trigger file path */
static char *dbreread_file;

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

/* Saved signal handers */
static struct sigaction old_sigint;
static struct sigaction old_sigterm;
static struct sigaction old_sigquit;
static struct sigaction old_sighup;


/**********************************************************************
 * Prototypes
 */

static unsigned selectSizeAdv(AlgQueue *algQ);

/**********************************************************************
 * Misc. helper functions
 */

static void chain(const struct sigaction *sa, int signal, siginfo_t *info, void *ptr)
{
	if (sa->sa_flags & SA_SIGINFO)
	{
		if (sa->sa_sigaction)
			sa->sa_sigaction(signal, info, ptr);
	}
	else
	{
		if (sa->sa_handler)
			sa->sa_handler(signal);
	}
}

static void sigint_handler(int signal, siginfo_t *info, void *ptr)
{
	struct sigaction *sa;

	finish = true;

	switch (signal)
	{
		case SIGINT:
			sa = &old_sigint;
			break;
		case SIGHUP:
			sa = &old_sighup;
			break;
		case SIGTERM:
			sa = &old_sigterm;
			break;
		case SIGQUIT:
			sa = &old_sigquit;
			break;
		default:
			sa = NULL;
			break;
	}
	if (sa)
		chain(sa, signal, info, ptr);
}

static void sighup_handler(int signal, siginfo_t *info, void *ptr)
{
	reload = true;
	chain(&old_sighup, signal, info, ptr);
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
		module = g_module_open(path, G_MODULE_BIND_LAZY);
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

static void addDownload(const DLException *e)
{
	if (!dbreread_file)
	{
		LOG(LOG_WARNING, "Plugin reported not supported URL, but unable "
			"to handle as DB reread file not set!");
		return;
	}
	DBHandler *dbh = DBHandler::get();
	vector<string> lnames = e->getInputs();
	auto_ptr<Job> job = dbh->getJob(e->getJobId());
	job->setStatus(Job::PREPARE);
	for (vector<string>::iterator it = lnames.begin(); it != lnames.end(); it++)
	{
		FileRef fr = job->getInputRef(*it);
		string url = fr.getURL();
		dbh->addDL(job->getId(), *it, url);
	}
	DBHandler::put(dbh);

	if (touch(dbreread_file))
	{
		LOG(LOG_WARNING, "Failed to touch DB reread trigger file '%s': %s",
			dbreread_file, strerror(errno));
	}
}

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
				try {
					handler->submitJobs(jobs);
				} catch (DLException *e) {
					addDownload(e);
					delete e;
				}
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
			try {
				handler->submitJobs(jobs);
			} catch (DLException *e) {
				addDownload(e);
				delete e;
			}
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
		if (handler)
			g_strstrip(handler);
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
	g_strstrip(plugin_dir);

	dbreread_file = g_key_file_get_string(global_config, GROUP_WSSUBMITTER, "dbreread-file", &error);
	if (!dbreread_file || error)
	{
		LOG(LOG_ERR, "Failed to get DB reread file's location: %s",
			error->message);
		g_error_free(error);
		dbreread_file = NULL;
	}
	if (dbreread_file)
		g_strstrip(dbreread_file);

	if (debug_mode)
	{
		log_init_debug();
		run_as_daemon = 0;
	}
	else
		log_init(global_config, GROUP_BRIDGE);

	if (run_as_daemon && pid_file_create(global_config, GROUP_BRIDGE))
		exit(EX_OSERR);

	DBHandler::init(global_config);
	modules = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, close_module);

	if (run_as_daemon)
	{
		daemon(0, 0);
		pid_file_update();
	}

	try {
		init_grid_handlers();

		LOG(LOG_DEBUG, "Reading algorithm data");
		AlgQueue::load();
	}
	catch (QMException *error) {
		LOG(LOG_ERR, "Fatal: %s", error->what());
		delete error;
		exit(EX_SOFTWARE);
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = sigint_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &sa, &old_sigint);
	sigaction(SIGTERM, &sa, &old_sigterm);
	sigaction(SIGQUIT, &sa, &old_sigquit);

	/* BOINC sends SIGHUP when it wants to kill the process */
	if (run_as_daemon)
		sa.sa_sigaction = sighup_handler;
	sigaction(SIGHUP, &sa, &old_sighup);

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
