/* -*- mode: c++; coding: utf-8-unix -*-
 * Copyright (C) 2008-2010 MTA SZTAKI LPDS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * In addition, as a special exception, MTA SZTAKI gives you permission to link the
 * code of its release of the 3G Bridge with the OpenSSL project's "OpenSSL"
 * library (or with modified versions of it that use the same license as the
 * "OpenSSL" library), and distribute the linked executables. You must obey the
 * GNU General Public License in all respects for all of the code used other than
 * "OpenSSL". If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version.
 */

#ifndef DBHANDLER_H
#define DBHANDLER_H

#include "Job.h"
#include "AlgQueue.h"
#include "GridHandler.h"
#include "QMException.h"

#include <string>
#include <memory>
#include <vector>
#include <map>

#include <mysql.h>
#include <glib.h>


using namespace std;

/**
 * Converts the given job status to its string equivalent.
 */
const char *statToStr(Job::JobStatus stat);

class DBResult;

/**
 * Histogram for the statuses of a meta-job's existing sub-jobs.  */
typedef map<Job::JobStatus, size_t> MJHistogram;

/**
 * Database handler class. This class is responsible for providing an interface
 * for the bride's database.
 */
class DBHandler {
    public:
	/// Destructor
	~DBHandler();

	/**
	 * Execute a query built up using a printf-like format.
	 * @param fmt format string
	 * @param ... arguments to the format string
	 * @return true is the query succeeded, false otherwise
	 */
	bool query(const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));

	/**
	 * Execute a query using the string argument.
	 * @param str the query to execute
	 * @return true is the query succeeded, false otherwise
	 */
	bool query(const string &str) { return query("%s", str.c_str()); }

	/**
	 * Get job from the database.
	 * The function reads job from the database matching the given
	 * identifier, and returns in a Job object.
	 * @param id job identifier to use
	 * @return pointer to the Job object
	 */
	auto_ptr<Job> getJob(const string &id);

	/**
	 * Get jobs from the database. The function reads jobs from the database
	 * matching the given arguments, and returns the in a JobVector. The
	 * jobs are ordered by their creation time in the query. Use this
	 * function for grids that support packaging based on algorithm names.
	 * @param[out] jobs JobVector storage for the jobs read
	 * @param grid grid name to use
	 * @param alg algorithm name to use
	 * @param stat job status to use
	 * @param batch maximum number of jobs to read
	 */
	void getJobs(JobVector &jobs, const string &grid, const string &alg, Job::JobStatus stat, unsigned batch);

	/**
	 * Get jobs from the database. The function reads jobs from the database
	 * matching the given arguments, and returns the in a JobVector. The
	 * jobs are ordered by their creation time in the query. Use this
	 * function for grids that do not consider algorithm names when creating
	 * packages.
	 * @param[out] jobs JobVector storage for the jobs read
	 * @param grid grid name to use
	 * @param stat job status to use
	 * @param batch maximum number of jobs to read
	 */
	void getJobs(JobVector &jobs, const string &grid, Job::JobStatus stat, unsigned batch);

	/**
	 * Get jobs from the database. The function reads jobs from the database
	 * matching the given grid identifier.
	 * @param[out] jobs JobVector storage for the jobs read
	 * @param gridID grid identifier to search for
	 */
	void getJobs(JobVector &jobs, const char *gridID);

	/**
	 * Polls database for jobs within a transaction. The function reads
	 * all jobs matching any of the given states. For every job read the
	 * given GridHandler's poll function is called. Thus, this function
	 * can be used to perform the same operation on every job read.
	 * @param handler the GridHandler plugin to use
	 * @param stat1 job status one
	 * @param stat2 job status two
	 */
	void pollJobs(GridHandler *handler, Job::JobStatus stat1, Job::JobStatus stat2);
	void pollJobs(void (*cb)(Job* job, void* user_data),
		      void *user_data, const string &grid, Job::JobStatus stat);

	/**
	 * Queries the database for finished (either successfully or erronously) jobs.
	 * @param[out] jobs JobVector storage for the jobs read
	 * @param grid grid name to use
	 * @param batch maximum number of jobs to read
	 */
	void getFinishedJobs(JobVector &jobs, const string &grid, unsigned batch);

	/**
	 * Select gridids from the database for which all job instances in the
	 * given grid have the given status.
	 * @param[out] ids vector of matching grid identifiers
	 * @param grid grid to look at
	 * @param stat job status to look at
	 */
	void getCompleteWUs(vector<string> &ids, const string &grid, Job::JobStatus stat);

	/**
	 * Select gridids from the database for which all have the given status.
	 * @param[out] ids vector of matching grid identifiers
	 * @param grid grid to look at
	 * @param stat job status to look at
	 */
	void getCompleteWUsSingle(vector<string> &ids, const string &grid, Job::JobStatus stat);

	/**
	 * Update a job's parent meta-job id
	 * @param ID the job's identifier
	 * @param metajobid the id of the parent meta-job
	 */
	void updateJobMetajobId(const string &ID, const string &metajobid);

	/**
	 * Update a job's grid identifier.
	 * @param ID the job's identifier
	 * @param gridID the grid identifier to set
	 */
	void updateJobGridID(const string &ID, const string &gridID);

	/**
	 * Update a job's grid data.
	 * @param ID the job's identifier
	 * @param gridData the grid data to set
	 */
	void updateJobGridData(const string &ID, const string &gridData);

	/**
	 * Update a job's tag.
	 * @param ID the job's identifier
	 * @param tag the tag to set
	 */
	void updateJobTag(const string &ID, const string &tag);

	/**
	 * Update a job's status.
	 * @param ID the job's identifier
	 * @param newstat the status to set
	 */
	void updateJobStat(const string &ID, Job::JobStatus newstat);

	/**
	 * Add a job to the database.
	 * @param job the Job object to add
	 * @return true is job has been added successfully, false otherwise
	 */
	bool addJob(Job &job);

	/**
	 * Delete a job from the database.
	 * @param ID the identifier of the job to delete
	 */
	void deleteJob(const string &ID);

	/**
	 * Delete a package of jobs.
	 * All jobs having the given grid identifier are deleted from the
	 * database.
	 * @param gridId the grid identifier to use
	 */
	void deleteBatch(const string &gridId);

	/**
	 * Load algorithm queue statistics.
	 */
	void loadAlgQStats(void);

	/**
	 * Update algorithm queue statistics.
	 * @param algQ the algorithm queue to use
	 * @param pSize the package size used
	 * @param pTime the processing time of the package
	 */
	void updateAlgQStat(AlgQueue *algQ, unsigned pSize, unsigned pTime);

	/**
	 * Update algorithm queue statistics. The algorithm queue is located
	 * based on the grid identifier provided.
	 * @param gridid the grid identifier of the package to use
	 * @param pSize the package size used
	 * @param pTime the processing time of the package
	 */
	void updateAlgQStat(const char *gridid, unsigned pSize, unsigned pTime);

	/**
	 * Marks a database handle as unused.
	 * @param dbh the DBHandler
	 */
	static void put(DBHandler *dbh);

	/**
	 * Gets a database handle. The handle must be returned via put() when
	 * no longer needed.
	 * @return pointer to a DBHandler
	 */
	static DBHandler *get() throw (QMException *);

	/**
	 * Add an algorithm queue to the database. Initially, the processing
	 * statistics data is set empty.
	 * @param grid the grid of the algorithm queue
	 * @param alg the algorithm name of the algorithm queue
	 * @param batchsize the maximum batch size of the algorithm queue
	 */
	void addAlgQ(const char *grid, const char *alg, unsigned batchsize);

	/**
	 * Get list of algorithms registered for a grid.
	 * @param grid the grid name to look for
	 * @return vector of algorithm names
	 */
	vector<string> getAlgs(const string &grid);

	/**
	 * Initialize the database system.
	 * This method must be called before any threads that use the database
	 * system are started.
	 * @param config configuration file object
	 */
	static void init(GKeyFile *config);

	/**
	 * Free resources held by the database system.
	 * Must be called after all threads have exited.
	 */
	static void done();

	/**
	 * Add a download.
	 * Adds a download to the database.
	 * @param jobid the job identifier of the download
	 * @param localName the local name of the file
	 * @param url the remote URL of the file
	 */
	void addDL(const string &jobid, const string &localName, const string &url);

	/**
	 * Delete a download.
	 * Removed a download from the database identified by a job identifier
	 * and a local file name.
	 * @param jobid the job identifier
	 * @param localName the local name of the file
	 */
	void deleteDL(const string &jobid, const string &localName);

	/**
	 * Update a download.
	 * Updates a download changing the download time and the number of
	 * retries.
	 * @param jobid the job's identifier
	 * @param localName the file's local name
	 * @param next the time of download
	 * @param retries the number of retries
	 */
	void updateDL(const string &jobid, const string &localName, const struct timeval &next,
		int retries);

	/**
	 * Get all downloads.
	 * Function to get all downloads and pass to a callback function.
	 * @param cb the callback function to invode for the downloads
	 */
	void getAllDLs(
		void (*cb)(void * user_data, const char *jobid, const char *localName,
			   const char *url, const struct timeval *next, int retries),
		void *user_data);

	size_t getDLCount(const string &jobid);
	/**
	 * Updata a download's input path.
	 * The function can be used to update the filesystem path of input
	 * files.
	 * @param jobid the job identifier
	 * @param localName the local name of the file
	 * @param path the new path of the file
	 */
	void updateInputPath(const string &jobid, const string &localName, const string &path);
	void updateInputPath(const string &jobid, const string &localName,
			     const FileRef &ref);
	void setMetajobChildrenStatus(const string &mjid, Job::JobStatus newstat);
	void changeJobArgs(const string &jobid, const string &jobargs);

	void getSubjobCounts(const string &jobid, size_t &all, size_t &err);
	void removeMetajobChildren(const string &jobid);
	void discardPendingSubjobs(const string &parentId);
	void cancelAndDeleteRemainingSubjobs(const string &parentId);

	void getFinishedSubjobs(const string &parentId,
				JobVector &jobs,
				size_t limit);
	map<string, size_t> getSubjobHisto(const string &jobid);

	void copyEnv(const string &srcId, const string &dstId);
	vector<string> getSubjobErrors(const string &metajobId);

    protected:
	/// DBResult friend class.
	friend class DBResult;

	/**
	 * DBHandler constructor.
	 */
	DBHandler();

	/// MySQL connection
	MYSQL *conn;

    private:
	/**
	 * Parse job using a DBResult.
	 * @param res DBResult belonging to a Job
	 * @return the Job object representing the given DBResult
	 */
	auto_ptr<Job> parseJob(DBResult &res);

	/**
	 * Parse jobs into a JobVector using results of a query. The function
	 * uses DBResult to loop through the results of a previously executed
	 * query and fill in the JobVector.
	 * @param[out] jobs the JobVector to place parsed jobs in
	 */
	void parseJobs(JobVector &jobs);

	/**
	 * Connect to the database.
	 */
	void connect(void);

	/**
	 * Check the connection and reconnect if neccessary.
	 */
	void check(void);

	/**
	 * Escape string for safe insert.
	 * The function creates the correctly escaped version of the string for
	 * a safe insert or update.
	 * @param input the input string to escape
	 * @return a newly allocated memory buffer containing the escaped string
	 */
	char *escape_string(const char *input);
};


/**
 * Database result class. Objects of this class store results of a query.
 * Functions provide way to loop through elements of the results and to
 * get fields in a result.
 */
class DBResult {
    public:
	/// Destructor
	~DBResult();

	/// Store results of a query in the database
	void store() throw();

	/// Read in results of a query
	void use() throw();

	/**
	 * Fetch the next result of a query.
	 * @return true if next result is available
	 */
	bool fetch() throw();

	/**
	 * Get a field in the next result.
	 * @param name name of the field
	 * @return value of the field
	 */
	const char *get_field(const char *name);

	/**
	 * Get a field in the next result.
	 * @param index index of the field
	 * @return value of the field
	 */
	const char *get_field(int index);

    protected:
	/// DBHandler friend class
	friend class DBHandler;

	/**
	 * Constructor.
	 * @param dbh DBHandler object related to the result
	 */
	DBResult(DBHandler *dbh):res(0),dbh(dbh) {};

    private:
	/// MySQL result
	MYSQL_RES *res;

	/// Related DBHandler object
	DBHandler *dbh;

	/// Row of the next result
	MYSQL_ROW row;

	/// Fields of the result
	MYSQL_FIELD *fields;

	/// Number of fields in the result
	int field_num;
};

/**
 * Implements finally clause to use DBHandler safely */
class DBHWrapper
{
protected:
	DBHandler *dbh;
public:
	DBHWrapper() { dbh = DBHandler::get(); }
	~DBHWrapper() { DBHandler::put(dbh); }

	DBHandler *operator->() { return dbh; }
	DBHandler *operator*() { return dbh; }
};

#endif /* DBHANDLER_H */
