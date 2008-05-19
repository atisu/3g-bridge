#include <time.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdarg.h>
#include <syslog.h>


using namespace std;


class Logging {
private:
	Logging(ostream *os, int lvl) { tos = os; loglevel = lvl; }
	static Logging *logco;
	static ostream *tos;
	int loglevel;
	void print_hdr(int lvl) {
		time_t now = time(NULL);
		struct tm *tm = localtime(&now);
		char buf[37];
		strftime(buf, sizeof(buf), "%F %T", tm);
		*tos << buf << ' ';
		switch (lvl) {
		case LOG_DEBUG:
			*tos << "DEBUG";
			break;
		case LOG_INFO:
			*tos << "INFO";
			break;
		case LOG_NOTICE:
			*tos << "NOTICE";
			break;
		case LOG_WARNING:
			*tos << "WARNING";
			break;
		case LOG_ERR:
			*tos << "ERROR";
			break;
		case LOG_CRIT:
			*tos << "CRITICAL";
			break;
		}
		*tos << ": ";
	}
public:
	static Logging &getInstance(ostream &os = cout, int lvl = LOG_INFO) {
		if (0 == logco)
			logco = new Logging(&os, lvl);
		return *logco;
	}

	void log(int lvl, const char *fmt, va_list ap) {
		if (lvl > loglevel)
			return;
		print_hdr(lvl);
		char *str;
		vasprintf(&str, fmt, ap);
		*tos << str << endl;
		free(str);
	}

	void log(int lvl, const char *fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		log(lvl, fmt, ap);
		va_end(ap);
	}

	void log(int lvl, const string &str) {
		log(lvl, str.c_str());
	}
};

#define LOG(y, ...)	Logging::getInstance().log(y, __VA_ARGS__)
