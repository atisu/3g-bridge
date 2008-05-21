#ifndef QMCONFIG_H
#define QMCONFIG_H

#include <map>

using namespace std;

class QMConfig
{
	public:
		QMConfig(const char *filename);
		~QMConfig() {};

		string getStr(const string key);
	private:
		map<string, string> data;
};

#endif /* QMCONFIG_H */
