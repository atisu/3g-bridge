#ifndef DBHANDLER_H
#define DBHANDLER_H

#include "Job.h"
#include "AlgQueue.h"
#include "GridHandler.h"
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
	void getJobs(JobVector &jobs, const string &grid, const string &alg, JobStatus stat, unsigned batch);
	void getJobs(JobVector &jobs, const string &grid, JobStatus stat, unsigned batch);
	void getJobs(JobVector &jobs, const char *gridID);
	void pollJobs(GridHandler *handler, JobStatus stat1, JobStatus stat2);
	void getCompleteWUs(vector<string> &ids, const string &grid, JobStatus stat);
	void updateJobGridID(const string &ID, const string &gridID);
	void updateJobStat(const string &ID, JobStatus newstat);
	void addJob(Job &job);
	void deleteJob(const string &ID);
	void deleteBatch(const string &gridId);
	void loadAlgQStats(void);
	void updateAlgQStat(AlgQueue *algQ, unsigned pSize, unsigned pTime);
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
	Job *parseJob(DBResult &res);
	void parseJobs(JobVector &jobs);
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
	void store() throw();
	void use() throw();
	bool fetch() throw();
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

#endif /* DBHANDLER_H */
