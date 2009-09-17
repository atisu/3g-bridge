/*
 * Copyright (C) 2009 MTA SZTAKI LPDS
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

#include <string>

#include <sysexits.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include "soap/MonitorH.h"
#include "soap/Monitor.nsmap"

#include "Conf.h"
#include "MonitorHandler.h"
#include "Util.h"

#include <glib.h>
#include <gmodule.h>

using namespace std;

/**********************************************************************
 * Global variables
 */

/* If 'true', exit was requested by a signal */
static volatile bool finish;

/* If 'true', the log file should be re-opened */
static volatile bool reload;

/* The global configuration */
static GKeyFile *global_config = NULL;

/* Config: Location of the plugin directory */
static char *plugin_dir;

/* Command line: Location of the config file */
static char *config_file = SYSCONFDIR "/3g-bridge.conf";

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

/* Set of loaded plugins */
static GHashTable *instances;
static GHashTable *modules;

/* Hack for gSoap */
struct Namespace namespaces[] = {{ NULL, }};

/**********************************************************************
 * Get a plugin instance by name
 */

static void close_module(void *ptr)
{
	g_module_close((GModule *)ptr);
}

/* Load the module and get retrieve the address of the factory symbol */
static monitor_factory_func get_factory(const char *handler)
{
	monitor_factory_func fn;
	GModule *module;

	module = (GModule *)g_hash_table_lookup(modules, handler);
	if (!module)
	{
		char *path = g_strdup_printf("%s/%s_monitor.%s", plugin_dir, handler, G_MODULE_SUFFIX);
		module = g_module_open(path, G_MODULE_BIND_LOCAL);
		g_free(path);
		if (!module)
		{
			LOG(LOG_ERR, "Failed to load plugin %s_monitor: %s", handler, g_module_error());
			return NULL;
		}
		g_hash_table_insert(modules, g_strdup(handler), module);
	}

	if (!g_module_symbol(module, G_STRINGIFY(MONITOR_FACTORY_SYMBOL), (void **)&fn))
	{
		LOG(LOG_ERR, "Failed to initialize plugin %s_monitor: %s", handler, g_module_error());
		return NULL;
	}
	LOG(LOG_INFO, "Loaded plugin %s_monitor", handler);
	return fn;
}

static void delete_instance(void *ptr)
{
	MonitorHandler *instance = (MonitorHandler *)ptr;

	delete instance;
}

/* Create a new MonitorHandle instance for the specified grid */
static MonitorHandler *get_instance(const char *grid)
{
	MonitorHandler *instance = 0;
	monitor_factory_func fn;
	char *handler;

	if (!g_key_file_has_group(global_config, grid))
	{
		LOG(LOG_ERR, "Unknown grid requested: %s", grid);
		return 0;
	}
	
	instance = (MonitorHandler *)g_hash_table_lookup(instances, grid);
	if (instance)
		return instance;

	handler = g_key_file_get_string(global_config, grid, "handler", NULL);
	if (!handler)
	{
		LOG(LOG_ERR, "No handler is defined for grid %s", grid);
		return 0;
	}

	fn = get_factory(handler);
	g_free(handler);
	if (!fn)
		return 0;

	instance = fn(global_config, grid);
	if (instance)
		g_hash_table_insert(instances, g_strdup(grid), instance);
	return instance;
}

/**********************************************************************
 * Web service routines
 */

int __G3BridgeMonitor__getRunningJobs(struct soap *soap, std::string grid, unsigned int &resp)
{
	MonitorHandler *handler;

	handler = get_instance(grid.c_str());
	if (!handler)
		return soap_sender_fault(soap, "Bad grid name", NULL);

	resp = handler->getRunningJobs();

	return SOAP_OK;
}

int __G3BridgeMonitor__getWaitingJobs(struct soap *soap, std::string grid, unsigned int &resp)
{
	MonitorHandler *handler;

	handler = get_instance(grid.c_str());
	if (!handler)
		return soap_sender_fault(soap, "Bad grid name", NULL);

	resp = handler->getWaitingJobs();

	return SOAP_OK;
}

int __G3BridgeMonitor__getCPUCount(struct soap *soap, std::string grid, unsigned int &resp)
{
	MonitorHandler *handler;

	handler = get_instance(grid.c_str());
	if (!handler)
		return soap_sender_fault(soap, "Bad grid name", NULL);

	resp = handler->getCPUCount();

	return SOAP_OK;
}

int __G3BridgeMonitor__getVersion(struct soap *soap, std::string &resp)
{
	resp = PACKAGE_STRING;
	return SOAP_OK;
}

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
 * The main program
 */

int main(int argc, char **argv)
{
	GOptionContext *context;
	GError *error = NULL;
	struct sigaction sa;
	struct soap soap;
	int port;

	/* Parse the command line */
	context = g_option_context_new("- Web Service monitoring interface to the 3G Bridge");
	g_option_context_add_main_entries(context, options, PACKAGE);
        if (!g_option_context_parse(context, &argc, &argv, &error))
	{
		LOG(LOG_ERR, "Failed to parse the command line options: %s", error->message);
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
		LOG(LOG_ERR, "The configuration file is not specified");
		exit(EX_USAGE);
	}

	global_config = g_key_file_new();
	g_key_file_load_from_file(global_config, config_file, G_KEY_FILE_NONE, &error);
	if (error)
	{
		LOG(LOG_ERR, "Failed to load the config file: %s", error->message);
		g_error_free(error);
		exit(EX_NOINPUT);
	}

	if (kill_daemon)
		exit(pid_file_kill(global_config, GROUP_WSMONITOR));

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
		log_init(global_config, GROUP_WSMONITOR);

	port = g_key_file_get_integer(global_config, GROUP_WSMONITOR, "port", &error);
	if (!port || error)
	{
		LOG(LOG_ERR, "Failed to retrieve the listener port: %s", error->message);
		g_error_free(error);
		exit(EX_DATAERR);
	}
	if (port <= 0 || port > 65535)
	{
		LOG(LOG_ERR, "Invalid port number (%d) specified", port);
		exit(EX_DATAERR);
	}

	if (run_as_daemon && pid_file_create(global_config, GROUP_WSMONITOR))
		exit(EX_OSERR);

	modules = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, close_module);
	instances = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, delete_instance);

	/* Set up the signal handlers */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	/* BOINC sends SIGHUP when it wants to kill the process */
	if (run_as_daemon)
		sa.sa_handler = sighup_handler;
	sigaction(SIGHUP, &sa, NULL);

	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);

	if (run_as_daemon)
	{
		daemon(0, 0);
		pid_file_update();
	}

	soap_init2(&soap, SOAP_IO_KEEPALIVE, SOAP_IO_KEEPALIVE | SOAP_IO_CHUNK);
	soap_set_namespaces(&soap, Monitor_namespaces);
	soap.send_timeout = 60;
	soap.recv_timeout = 60;
	/* Give a small accept timeout to detect exit signals quickly */
	soap.accept_timeout = 1;
	soap.max_keep_alive = 100;
	soap.socket_flags = MSG_NOSIGNAL;
	soap.bind_flags = SO_REUSEADDR;

	SOAP_SOCKET ss = soap_bind(&soap, NULL, port, 100);
	if (!soap_valid_socket(ss))
	{
		char buf[256];
		soap_sprint_fault(&soap, buf, sizeof(buf));
		LOG(LOG_ERR, "SOAP initialization: %s", buf);
		exit(EX_UNAVAILABLE);
	}

	while (!finish)
	{
		if (reload)
		{
			log_reopen();
			reload = false;
		}

		SOAP_SOCKET ret = soap_accept(&soap);
		if (ret == SOAP_INVALID_SOCKET)
		{
			if (!soap.errnum)
				continue;
			soap_print_fault(&soap, stderr);
			/* XXX Should we really exit here? */
			exit(EX_UNAVAILABLE);
		}

		struct soap *handler = soap_copy(&soap);
		LOG(LOG_DEBUG, "Serving SOAP request");
		try
		{
			Monitor_serve(handler);
		}
		catch (...)
		{
			LOG(LOG_ERR, "SOAP: Caught unhandled exception");
		}

		soap_destroy(handler);
		soap_end(handler);
		soap_free(handler);

		LOG(LOG_DEBUG, "Finished serving SOAP request");
	}

	LOG(LOG_DEBUG, "Signal caught, shutting down");

	soap_destroy(&soap);
	soap_end(&soap);
	soap_done(&soap);

	g_hash_table_destroy(instances);
	g_hash_table_destroy(modules);

	g_key_file_free(global_config);
	g_free(plugin_dir);

	return EX_OK;
}
