#define _GNU_SOURCE
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Conf.h"
#include "Util.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>

#include <glib.h>

static char *log_file_name;
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

static int pid_fd = -1;
static char *pid_name;

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
		g_free(log_file_name);
	}
	if (use_syslog)
	{
		closelog();
		use_syslog = FALSE;
	}
}

void log_init(GKeyFile *config, const char *section)
{
	char *str;
	
	str = g_key_file_get_string(config, section, "log-level", NULL);
	if (!str)
		str = g_key_file_get_string(config, GROUP_DEFAULTS, "log-level", NULL);
	if (str)
		set_level(str);
	g_free(str);

	str = g_key_file_get_string(config, section, "log-target", NULL);
	if (!str)
		str = g_key_file_get_string(config, GROUP_DEFAULTS, "log-target", NULL);
	if (!str || !g_strcasecmp(str, "stdout"))
		log_file = stdout;
	else if (!g_strncasecmp(str, "syslog", 6))
	{
		use_syslog = TRUE;
		if (str[6] == ':')
			set_facility(str + 7);
		openlog(section, LOG_PID, log_facility);
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
		else
		{
			setvbuf(log_file, NULL, _IOLBF, 1024);
			log_file_name = g_strdup(path);
		}
	}
	g_free(str);

	atexit(log_cleanup);
}

void log_reopen(void)
{
	if (use_syslog || !log_file_name)
		return;

	if (log_file)
		fclose(log_file);
	log_file = fopen(log_file_name, "a");
	/* We could check for an error but we can not report it... */
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

static void pid_file_remove(void)
{
	if (pid_fd != -1)
	{
		close(pid_fd);
		pid_fd = -1;
	}
	if (pid_name)
	{
		unlink(pid_name);
		g_free(pid_name);
		pid_name = NULL;
	}
}

int pid_file_create(GKeyFile *config, const char *section)
{
	struct flock lck;
	int fd, ret;
	char *str;
	
	str = g_key_file_get_string(config, section, "pid-file", NULL);
	if (!str)
		str = g_strdup_printf("%s/%s.pid", RUNDIR, section);

	fd = open(str, O_RDWR | O_CREAT, 0644);
	if (fd == -1)
	{
		fprintf(stderr, "Failed to create the PID file %s: %s\n", str, strerror(errno));
		g_free(str);
		return -1;
	}

	memset(&lck, 0, sizeof(lck));
	lck.l_type = F_WRLCK;
	lck.l_whence = SEEK_SET;

	ret = fcntl(fd, F_SETLK, &lck);
	if (ret)
	{
		if (errno == EAGAIN || errno == EACCES)
			fprintf(stderr, "Another instance of the daemon is already running\n");
		else
			fprintf(stderr, "Failed to lock the PID file %s: %s", str, strerror(errno));
		g_free(str);
		close(fd);
		return -1;
	}

	pid_fd = fd;
	pid_name = str;

	atexit(pid_file_remove);
	return 0;
}

void pid_file_update(void)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());
	lseek(pid_fd, 0, SEEK_SET);
	write(pid_fd, buf, strlen(buf));
	ftruncate(pid_fd, strlen(buf));
}
