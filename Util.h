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

#ifndef UTIL_H
#define UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <syslog.h>

#include <glib.h>

/**
 * Initialize the logging system.
 * @param config global configuration
 * @param section section to use inside the configuration
 */
void log_init(GKeyFile *config, const char *section);

/**
 * Initialize the logging system in debug mode.
 */
void log_init_debug(void);

/**
 * Re-open the log file if needed.
 */
void log_reopen(void);

/**
 * Print a log message.
 * @param lvl the log level
 * @param fmt format string
 * @param ... arguments matching the format string
 */
void logit(int lvl, const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));

/**
 * Print a log message.
 * @param lvl the log level
 * @param fmt format string
 * @param ap argument list matching the format string
 */
void vlogit(int lvl, const char *fmt, va_list ap);

/**
 * Create a pid file.
 * @param config the global configuration
 * @param section the section to use inside the configuration
 */
int pid_file_create(GKeyFile *config, const char *section);

/**
 * Update the pid file with the actual pid of the program.
 */
void pid_file_update(void);

/**
 * Kill a running daemon.
 */
int pid_file_kill(GKeyFile *config, const char *section);

/**
 * Touch a file.
 * @param fname the file to touch
 */
int touch(const char *fname);

#ifdef __cplusplus
}
#endif

#define LOG(y, ...)    logit(y, __VA_ARGS__)

#endif /* UTIL_H */
