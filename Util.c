#define _GNU_SOURCE
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Conf.h"
#include "Util.h"

#include <sysexits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>

#include <glib.h>

typedef enum { SYSLOG, LOGFILE, STDOUT } mode;

static char *log_file_name;
static FILE *log_file;
static mode log_mode = SYSLOG;

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

/* We do not want to allow just any facility, only a few */
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
	switch (log_mode)
	{
		case LOGFILE:
			if (log_file)
				fclose(log_file);
			log_file = NULL;
			g_free(log_file_name);
			break;
		case SYSLOG:
			closelog();
			break;
		default:
			break;
	}

	log_mode = STDOUT;
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
	if (!str)
		goto out;

	if (!g_strcasecmp(str, "stdout"))
		log_mode = STDOUT;
	else if (!g_strncasecmp(str, "syslog", 6))
	{
		log_mode = SYSLOG;
		if (str[6] == ':')
			set_facility(str + 7);
		openlog(section, LOG_PID, log_facility);
	}
	else if (!g_strncasecmp(str, "file:", 5) || str[0] == '/')
	{
		char *path = str;

		log_mode = LOGFILE;
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

out:
	atexit(log_cleanup);
}

void log_init_debug(void)
{
	log_mode = STDOUT;
	log_level = LOG_DEBUG;
}

void log_reopen(void)
{
	if (log_mode != LOGFILE)
		return;

	if (log_file)
		fclose(log_file);
	log_file = fopen(log_file_name, "a");
	/* We could check for an error but we can not report it... */
}

void vlogit(int lvl, const char *fmt, va_list ap)
{
	const char *lvl_str = "UNKNOWN";
	char timebuf[37], *msg;
	struct tm tm;
	time_t now;

	if (lvl > log_level)
		return;

	if (log_mode == SYSLOG)
	{
		vsyslog(lvl, fmt, ap);
		return;
	}

	now = time(NULL);
	localtime_r(&now, &tm);

	strftime(timebuf, sizeof(timebuf), "%F %T", &tm);

	if (lvl >= 0 && lvl < (int)(sizeof(level_str) / sizeof(level_str[0])))
		lvl_str = level_str[lvl];

	vasprintf(&msg, fmt, ap);
	if (log_mode == STDOUT)
		fprintf(stdout, "%s %s: %s\n", timebuf, lvl_str, msg);
	else if (log_file)
		fprintf(log_file, "%s %s: %s\n", timebuf, lvl_str, msg);
	free(msg);
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
	char *str;
	int fd;
	
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

	if (fcntl(fd, F_SETLK, &lck))
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
	struct flock lck;
	char buf[32];

	memset(&lck, 0, sizeof(lck));
	lck.l_type = F_WRLCK;
	lck.l_whence = SEEK_SET;

	/* fcntl locks are not inherited though fork() so we have to lock
	 * the pid file again. Yes, this is racy. */
	if (fcntl(pid_fd, F_SETLKW, &lck))
	{
		fprintf(stderr, "Failed to lock the PID file %s: %s", pid_name, strerror(errno));
		return;
	}

	snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());
	lseek(pid_fd, 0, SEEK_SET);
	write(pid_fd, buf, strlen(buf));
	ftruncate(pid_fd, strlen(buf));
}

int pid_file_kill(GKeyFile *config, const char *section)
{
	char *str, buf[16], *p;
	struct flock lck;
	int fd, ret, i;
	pid_t pid;

	str = g_key_file_get_string(config, section, "pid-file", NULL);
	if (!str)
		str = g_strdup_printf("%s/%s.pid", RUNDIR, section);

	fd = open(str, O_RDWR);
	if (fd == -1)
	{
		if (errno == ENOENT)
		{
			LOG(LOG_DEBUG, "No pid file, the daemon is not running");
			ret = EX_OK;
		}
		else
		{
			LOG(LOG_ERR, "Failed to open the pid file %s: %s", str, strerror(errno));
			ret = EX_IOERR;
		}
		g_free(str);
		return ret;
	}

	memset(&lck, 0, sizeof(lck));
	lck.l_type = F_WRLCK;
	lck.l_whence = SEEK_SET;

	ret = fcntl(fd, F_SETLK, &lck);
	if (!ret)
	{
		LOG(LOG_INFO, "Stale pid file, removing");
		unlink(str);
		g_free(str);
		close(fd);
		return EX_OK;
	}
	if (errno != EACCES && errno != EAGAIN)
	{
		LOG(LOG_ERR, "Failed to lock the pid file %s: %s", str, strerror(errno));
		g_free(str);
		close(fd);
		return EX_OSERR;
	}

	ret = read(fd, buf, sizeof(buf) - 1);
	if (ret == -1)
	{
		LOG(LOG_ERR, "Failed to read the pid file %s: %s", str, strerror(errno));
		g_free(str);
		close(fd);
		return EX_OSERR;
	}

	close(fd);

	buf[ret] = '\0';
	pid = strtol(buf, &p, 10);
	if (p && *p && *p != '\n')
	{
		LOG(LOG_ERR, "Garbage in the pid file");
		g_free(str);
		return EX_DATAERR;
	}

	ret = kill(pid, SIGTERM);
	if (ret)
	{
		LOG(LOG_ERR, "Failed to kill pid %ld: %s", (long)pid, strerror(errno));
		g_free(str);
		return FALSE;
	}

	LOG(LOG_DEBUG, "Signal sent");

	for (i = 0; i < 20; i++)
	{
		if (access(str, R_OK))
		{
			g_free(str);
			return EX_OK;
		}
		usleep(100000);
	}

	LOG(LOG_ERR, "Timeout waiting for the daemon to exit");
	g_free(str);
	return EX_UNAVAILABLE;
}
