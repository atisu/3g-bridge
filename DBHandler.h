#ifndef __DBHANDLER_H
#define __DBHANDLER_H

#include "CGJob.h"
#include "CGAlgQueue.h"
#include "QMException.h"

#include <string>

#include <mysql.h>


using namespace std;

class DBPool;
class DBResult;

class DBHandler {
    public:
	~DBHandler();
	bool query(const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));
	bool query(const string &str) { return query("%s", str.c_str()); }
	vector<CGJob *> *getJobs(const string &grid, const string &alg, CGJobStatus stat, int batch);
	vector<CGJob *> *getJobs(const string &grid, CGJobStatus stat, int batch);
	vector<CGJob *> *getJobs(const char *gridID);
	void updateJobGridID(const string &ID, const string &gridID);
	void updateJobStat(const string &ID, CGJobStatus newstat);
	void addJob(CGJob &job);
	void deleteJob(const string &ID);
	void loadAlgQStats(void);
	void updateAlgQStat(CGAlgQueue *algQ, unsigned pSize, unsigned pTime);
	void updateAlgQStat(const char *gridid, unsigned pSize, unsigned pTime);

	static void put(DBHandler *dbh);
	static DBHandler *get() throw (QMException &);

	void addAlgQ(const char *grid, const char *alg, unsigned batchsize);
    protected:
	friend class DBPool;
	friend class DBResult;
	DBHandler(const char *dbname, const char *host, const char *user, const char *passwd);
	MYSQL *conn;
    private:
	const char *getStatStr(CGJobStatus stat);
	vector<CGJob *> *parseJobs(void);
};

class DBPool
{
    public:
	~DBPool();
    protected:
	friend class DBHandler;
	DBHandler *get() throw (QMException &);
	void put(DBHandler *dbh);
    private:
	void init(void);

	unsigned max_connections;
	char *dbname;
	char *host;
	char *user;
	char *passwd;
	vector<DBHandler *> used_dbhs;
	vector<DBHandler *> free_dbhs;
};

class DBResult {
    public:
	~DBResult();
	void store();
	void use();
	bool fetch();
	const char *get_field(const char *name);
	const char *get_field(int index);
    protected:
	friend class DBHandler;
	DBResult(DBHandler *dbh):res(0),dbh(dbh) {};
    private:
	MYSQL_RES *res;
	DBHandler *dbh;
	MYSQL_ROW row;
	MYSQL_FIELD *fields;
	int field_num;
};

#endif /* __DBHANDLER_H */
