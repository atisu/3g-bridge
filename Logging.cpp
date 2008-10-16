#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Logging.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>

static ostream *log_stream;
G_LOCK_DEFINE_STATIC(log_stream);
static int log_level;
static const char *level_str[] =
{
	"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"
};

static void print_header(int level)
{
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	char buf[37];
	strftime(buf, sizeof(buf), "%F %T", tm);

	const char *str = "UNKNOWN";
	if (level >= 0 && level < (int)(sizeof(level_str) / sizeof(level_str[0])))
		str = level_str[level];

	*log_stream << buf << ' ' << str << ": ";
}

void Logging::init(ostream &stream, int level)
{
	log_stream = &stream;
	log_level = level;
}

void Logging::init(ostream &stream, const char *level)
{
	int i;

	for (i = 0; i < (int)(sizeof(level_str) / sizeof(level_str[0])); i++)
	{
		if (!strcasecmp(level, level_str[i]))
		{
			init(stream, i);
			return;
		}
	}
	init(stream, LOG_INFO);
	log(LOG_WARNING, "Failed to interpret log level %s, using INFO", level);
}

void Logging::vlog(int lvl, const char *fmt, va_list ap) {
	char *str;

	if (lvl > log_level)
		return;

	G_LOCK(log_stream);
	print_header(lvl);
	vasprintf(&str, fmt, ap);
	*log_stream << str << endl;
	G_UNLOCK(log_stream);
	free(str);
}

void Logging::log(int lvl, const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	Logging::vlog(lvl, fmt, ap);
	va_end(ap);
}

void Logging::log(int lvl, const string &str) {
	Logging::log(lvl, str.c_str());
}
