#ifndef __DBHANDLER_H
#define __DBHANDLER_H

#include <string>
#include "CGJob.h"
#include "CGAlgQueue.h"

#include <mysql++/mysql++.h>
#ifdef HAVE_MYSQL___SSQLS_H
#include <mysql++/ssqls.h>
#else
#include <mysql++/custom.h>
#endif


using namespace std;
using namespace mysqlpp;


class DBHandler {
    public:
	DBHandler(const string host, const string user, const string passwd, const string dbname);
	~DBHandler();
	vector<CGJob *> *getJobs(CGJobStatus stat);
	vector<CGJob *> *getJobs(string gridID);
	void updateJobGridID(string ID, string gridID);
	void updateJobStat(string ID, CGJobStatus newstat);
	void updateOutputPath(string ID, string localname, string pathname);
	string getAlgQStat(CGAlgType type, const string &name);
	void updateAlgQStat(CGAlgQueue *algQ, unsigned pSize, unsigned pTime);
 private:
	string thost;
	string tuser;
	string tpasswd;
	string tdbname;
	Connection *conn;
	string getStatStr(CGJobStatus stat);
	string Alg2Str(CGAlgType type);
	CGAlgType Str2Alg(const string name);
	vector<CGJob *> *parseJobs(Query *squery);
};

#endif /* __DBHANDLER_H */
