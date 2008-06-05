#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Logging.h"
#include "CGQueueManager.h"
#include "CGManager.h"

#ifdef HAVE_DCAPI
#include "DCAPIHandler.h"
#endif
#ifdef HAVE_EGEE
#include "EGEEHandler.h"
#endif

using namespace std;

GKeyFile *global_config = NULL;

static GHashTable *plugins;

void registerPlugin(const char *name, plugin_constructor ctor)
{
	if (!plugins)
		plugins = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
	g_hash_table_insert(plugins, (void *)name, (void *)ctor);
}

GridHandler *getPluginInstance(GKeyFile *config, const char *plugin, const char *instance)
{
	plugin_constructor ctor = (plugin_constructor)g_hash_table_lookup(plugins, plugin);
	if (!ctor)
		throw QMException("Request for unknown plugin %s", plugin);
	return ctor(config, instance);
}

int main(int argc, char **argv)
{
    GError *error;

    Logging::init(cout, LOG_INFO);

    if (argc < 2)
    {
	    cerr << "Missing config. file name" << endl;
	    exit(1);
    }
    if (argc > 2)
    {
	    cerr << "Extra arguments on the command-line" << endl;
	    exit(1);
    }

#ifdef HAVE_DCAPI
    registerPlugin("DC-API", DCAPIHandler::getInstance);
#endif
#ifdef HAVE_EGEE
    registerPlugin("EGEE", EGEEHandler::getInstance);
#endif

    global_config = g_key_file_new();
    error = NULL;
    g_key_file_load_from_file(global_config, argv[1], G_KEY_FILE_NONE, &error);
    if (error)
    {
	    cerr << "Failed to load the config file: " << error->message << endl;
	    exit(1);
    }

    char *level = g_key_file_get_string(global_config, "defaults", "log-level", NULL);
    if (level)
    {
	    Logging::init(cout, level);
	    g_free(level);
    }

    try {
	LOG(LOG_DEBUG, "Creating Queue Manager");
	CGQueueManager qm(global_config);

	LOG(LOG_DEBUG, "Starting Queue Manager");
	qm.run();
    } catch (QMException &error) {
	LOG(LOG_CRIT, "Caught an unhandled exception: %s", error.what());
	return -1;
    }

    g_key_file_free(global_config);

    return 0;
}
