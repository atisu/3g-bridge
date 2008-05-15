#include <time.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace std;

enum loglevel {
	DEB,
	INF,
	WARN,
	ERR,
	CRIT
};

class Logging {
private:
	Logging(ostream *os, loglevel lvl) { tos = os; tlvl = lvl; }
	static Logging *logco;
	static ostream *tos;
	loglevel tlvl;
public:
	static Logging &getInstance(ostream &os = cout, loglevel lvl = INF) {
		if (0 == logco)
			logco = new Logging(&os, lvl);
		return *logco;
	}
	void log(loglevel lvl, const string &func, const string &data) {
		if (lvl < tlvl)
			return;
	        time_t tt = time(NULL);
		struct tm *ttm = localtime(&tt);
		*tos << 1900+ttm->tm_year << ".";
		*tos << setw(2) << setfill('0') << ttm->tm_mon << ".";
		*tos << setw(2) << setfill('0') << ttm->tm_mday << ". ";
		*tos << setw(2) << setfill('0') << ttm->tm_hour << ":";
		*tos << setw(2) << setfill('0') << ttm->tm_min << ":";
		*tos << setw(2) << setfill('0') << ttm->tm_sec << " - ";
		*tos << func << " ";
		switch (lvl) {
		case DEB:
			*tos << "DEBUG";
			break;
		case INF:
			*tos << "INFO";
			break;
		case WARN:
			*tos << "WARNING";
			break;
		case ERR:
			*tos << "ERROR";
			break;
		case CRIT:
			*tos << "CRITICAL";
			break;
		}
		*tos << " - " << data << endl;
	}
};


#define LOG(y, x) { Logging a = Logging::getInstance(); a.log(y, __func__, x); }