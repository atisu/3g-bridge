#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdarg.h>
#include <syslog.h>

using namespace std;

namespace Logging
{
	void init(ostream &stream = cout, int level = LOG_INFO);
	void init(ostream &stream, const char *level);
	void log(int lvl, const char *fmt, va_list ap);
	void log(int lvl, const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));
	void log(int lvl, const string &str);
};

#define LOG(y, ...)	Logging::log(y, __VA_ARGS__)
