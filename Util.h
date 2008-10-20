#ifndef UTIL_H
#define UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <syslog.h>

#include <glib.h>

void log_init(GKeyFile *config, const char *section);
void log_reopen(void);

void logit(int lvl, const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));
void vlogit(int lvl, const char *fmt, va_list ap);

int pid_file_create(GKeyFile *config, const char *section);
void pid_file_update(void);

#ifdef __cplusplus
}
#endif

/* XXX Kill this macro, it's useless now */
#define LOG(y, ...)	logit(y, __VA_ARGS__)

#endif /* UTIL_H */
