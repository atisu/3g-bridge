#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "QMConfig.h"
#include "Logging.h"
#include "QMException.h"

#include <iostream>
#include <fstream>
#include <exception>

#include <ctype.h>

using namespace std;

QMConfig::QMConfig(const char *filename)
{
	ifstream file(filename, ios::in);
	if (file.fail())
		throw QMException("Failed to open config file %s", filename);

	string line;
	int cnt = 0;
	while (getline(file, line))
	{
		cnt++;
		string::iterator p = line.begin();

		/* Drop initial white space */
		while (p != line.end() && isspace(*p))
			p++;

		/* Skip comments */
		if (p == line.end() || *p == '#')
			continue;

		/* Extract the key */
		string key;
		while (p != line.end() && (isalnum(*p) || *p == '_'))
			key.push_back(*p++);

		if (!key.length())
		{
			LOG(LOG_ERR, "Syntax error in %s line %d: Key is missing", filename, cnt);
			continue;
		}

		/* Drop extra white space */
		while (p != line.end() && isspace(*p))
			p++;

		/* Look for '=' */
		if (p == line.end() || *p != '=')
		{
			LOG(LOG_ERR, "Syntax error in %s, line %d: missing '='", filename, cnt);
			continue;
		}
		p++;

		/* Drop extra white space */
		while (p != line.end() && isspace(*p))
			p++;

		/* Extract the value */
		string value;
		while (p != line.end())
			value.push_back(*p++);
		
		data[key] = value;
	}
}

string QMConfig::getStr(const string key)
{
	map<string, string>::const_iterator it = data.find(key);
	if (it != data.end())
		return it->second;
	return string("");
}
