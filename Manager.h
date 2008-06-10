#ifndef MANAGER_H
#define MANAGER_H

using namespace std;

#include <GridHandler.h>
#include <glib.h>

typedef GridHandler *(*plugin_constructor)(GKeyFile *config, const char *instance);

void registerPlugin(const char *name, plugin_constructor ctor);
GridHandler *getPluginInstance(GKeyFile *config, const char *plugin, const char *instance);

extern GKeyFile *global_config;

#endif /* MANAGER_H */
