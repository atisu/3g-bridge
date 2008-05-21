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
	void log(int lvl, const char *fmt, va_list ap);
	void log(int lvl, const char *fmt, ...);
	void log(int lvl, const string &str);
};

#define LOG(y, ...)	Logging::log(y, __VA_ARGS__)
