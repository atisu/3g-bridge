#ifndef LOGGING_H
#define LOGGING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <syslog.h>

#include <glib.h>

void log_init(GKeyFile *config, const char *argv0);
void logit(int lvl, const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));
void vlogit(int lvl, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#define LOG(y, ...)	logit(y, __VA_ARGS__)

#endif /* LOGGING_H */
