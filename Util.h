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

#ifdef __cplusplus
}
#endif

/* XXX Kill this macro, it's useless now */
#define LOG(y, ...)	logit(y, __VA_ARGS__)

#endif /* UTIL_H */
