#ifndef __DBHANDLER_H
#define __DBHANDLER_H

#include <string>
#include "CGJob.h"
#include "CGAlgQueue.h"
#include "QMConfig.h"

#include <mysql.h>


using namespace std;


class DBHandler {
    public:
	DBHandler(QMConfig &config);
	~DBHandler();
	bool query(const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));
	bool query(string &str) { return query("%s", str.c_str()); }
	vector<CGJob *> *getJobs(CGJobStatus stat);
	vector<CGJob *> *getJobs(const char *gridID);
	void updateJobGridID(string ID, string gridID);
	void updateJobStat(string ID, CGJobStatus newstat);
	void addJob(CGJob &job);
	void deleteJob(const string &ID);
	string getAlgQStat(CGAlgType type, const string &name, unsigned *ssize);
	void updateAlgQStat(CGAlgQueue *algQ, unsigned pSize, unsigned pTime);
	void updateAlgQStat(const char *gridid, unsigned pSize, unsigned pTime);
	void addAlgQ(const char *grid, const char *alg, unsigned batchsize);
    private:
	MYSQL *conn;
	const char *getStatStr(CGJobStatus stat);
	const char *Alg2Str(CGAlgType type);
	CGAlgType Str2Alg(const char *name);
	vector<CGJob *> *parseJobs(void);
};

class DBResult {
    public:
	DBResult():res(0) {};
	~DBResult();
	void store(MYSQL *dbh);
	bool fetch();
	const char *get_field(const char *name);
	const char *get_field(int index);
    private:
	MYSQL_RES *res;
	MYSQL_ROW row;
	MYSQL_FIELD *fields;
	int field_num;
};

#endif /* __DBHANDLER_H */
