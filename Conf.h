#ifndef CONF_H
#define CONF_H

#include <glib.h>

/*
 * This file holds definitions that are used by multiple components
 */

/* Special groups in the configuration file */
#define GROUP_DATABASE		"database"
#define GROUP_DEFAULTS		"defaults"
#define GROUP_BRIDGE		"bridge"
#define GROUP_WSSUBMITTER	"wssubmitter"

extern GKeyFile *global_config;

#endif /* CONF_H */
