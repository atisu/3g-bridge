#ifndef __DBHANDLER_H
#define __DBHANDLER_H

#include <string>
#include "CGJob.h"
#include "CGAlgQueue.h"
#include "QMConfig.h"

#include <mysql++/mysql++.h>


using namespace std;
using namespace mysqlpp;


class DBHandler {
    public:
	DBHandler(QMConfig &config);
	~DBHandler();
	vector<CGJob *> *getJobs(CGJobStatus stat);
	vector<CGJob *> *getJobs(string gridID);
	void updateJobGridID(string ID, string gridID);
	void updateJobStat(string ID, CGJobStatus newstat);
	void deleteJob(const string &ID);
	string getAlgQStat(CGAlgType type, const string &name);
	void updateAlgQStat(CGAlgQueue *algQ, unsigned pSize, unsigned pTime);
	void updateAlgQStat(const char *gridid, unsigned pSize, unsigned pTime);
    private:
	string host;
	string user;
	string passwd;
	string dbname;
	Connection *conn;
	const char *getStatStr(CGJobStatus stat);
	const char *Alg2Str(CGAlgType type);
	CGAlgType Str2Alg(const char *name);
	vector<CGJob *> *parseJobs(Query *squery);
};

#endif /* __DBHANDLER_H */
