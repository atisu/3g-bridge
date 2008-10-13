#ifndef LOGGING_H
#define LOGGING_H

#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <stdarg.h>
#include <syslog.h>

using namespace std;

namespace Logging
{
	void init(ostream &stream = cout, int level = LOG_INFO);
	void init(ostream &stream, const char *level);
	void vlog(int lvl, const char *fmt, va_list ap);
	void log(int lvl, const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));
	void log(int lvl, const string &str);
};

#define LOG(y, ...)	Logging::log(y, __VA_ARGS__)

#endif /* LOGGING_H */
