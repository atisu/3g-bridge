#define _GNU_SOURCE
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Conf.h"
#include "Logging.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

#include <glib.h>

static FILE *log_file;
static int use_syslog;

static int log_level = LOG_INFO;
static int log_facility = LOG_DAEMON;

static const char *level_str[] =
{
	[LOG_EMERG] = "EMERG",
	[LOG_ALERT] = "ALERT",
	[LOG_CRIT] = "CRIT",
	[LOG_ERR] = "ERROR",
	[LOG_WARNING] = "WARNING",
	[LOG_NOTICE] = "NOTICE",
	[LOG_INFO] = "INFO",
	[LOG_DEBUG] = "DEBUG"
};

/* We do not want to allow just any facility, only a few one */
static const char *facility_str[] =
{
	[LOG_DAEMON] = "daemon",
	[LOG_USER] = "user",
	[LOG_LOCAL0] = "local0",
	[LOG_LOCAL1] = "local1",
	[LOG_LOCAL2] = "local2",
	[LOG_LOCAL3] = "local3",
	[LOG_LOCAL4] = "local4",
	[LOG_LOCAL5] = "local5",
	[LOG_LOCAL6] = "local6",
	[LOG_LOCAL7] = "local7"
};

static void set_level(const char *level)
{
	unsigned i;

	for (i = 0; i < sizeof(level_str) / sizeof(level_str[0]); i++)
	{
		if (level_str[i] && !g_strcasecmp(level, level_str[i]))
		{
			log_level = i;
			return;
		}
	}
	logit(LOG_ERR, "Unknown log level (%s) specified, using default", level);
}

static void set_facility(const char *facility)
{
	unsigned i;

	for (i = 0; i < sizeof(facility_str) / sizeof(facility_str[0]); i++)
	{
		if (facility_str[i] && !g_strcasecmp(facility, facility_str[i]))
		{
			log_facility = i;
			return;
		}
	}
	logit(LOG_ERR, "Unknown facility (%s) specified, using default", facility);
}

static void log_cleanup(void)
{
	if (log_file && log_file != stdout)
	{
		fclose(log_file);
		log_file = NULL;
	}
	if (use_syslog)
	{
		closelog();
		use_syslog = FALSE;
	}
}

void log_init(GKeyFile *config, const char *argv0)
{
	char *str;
	
	str = g_key_file_get_string(config, GROUP_DEFAULTS, "log-level", NULL);
	if (str)
		set_level(str);
	g_free(str);

	str = g_key_file_get_string(config, GROUP_DEFAULTS, "log-target", NULL);
	if (!str || !g_strcasecmp(str, "stdout"))
		log_file = stdout;
	else if (!g_strncasecmp(str, "syslog", 6))
	{
		use_syslog = TRUE;
		if (str[6] == ':')
			set_facility(str + 7);
		openlog(argv0, LOG_PID, log_facility);
	}
	else if (!g_strncasecmp(str, "file:", 5) || str[0] == '/')
	{
		char *path = str;

		/* Handle the "file:/foo/bar" syntax */
		if (*path != '/')
			path += 5;

		log_file = fopen(path, "a");
		if (!log_file)
			logit(LOG_ERR, "Failed to open the log file %s: %s", path, strerror(errno));
	}
	g_free(str);

	atexit(log_cleanup);
}

void vlogit(int lvl, const char *fmt, va_list ap)
{
	if (lvl > log_level)
		return;

	if (use_syslog)
		vsyslog(lvl, fmt, ap);
	else
	{
		const char *lvl_str = "UNKNOWN";
		char timebuf[37], *msg;
		struct tm tm;
		time_t now;

		now = time(NULL);
		localtime_r(&now, &tm);

		strftime(timebuf, sizeof(timebuf), "%F %T", &tm);

		if (lvl >= 0 && lvl < (int)(sizeof(level_str) / sizeof(level_str[0])))
			lvl_str = level_str[lvl];

		vasprintf(&msg, fmt, ap);
		if (!log_file)
			fprintf(stdout, "%s %s: %s\n", timebuf, lvl_str, msg);
		else
			fprintf(log_file, "%s %s: %s\n", timebuf, lvl_str, msg);
		free(msg);
	}
}

void logit(int lvl, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vlogit(lvl, fmt, ap);
	va_end(ap);
}
