/* -*- mode: c++; coding: utf-8-unix -*-
 *
 * Copyright (C) 2009-2010 MTA SZTAKI LPDS
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

#ifndef METAJOB_HANDLER_H
#define METAJOB_HANDLER_H

#include "Job.h"
#include "GridHandler.h"
#include "MJ_parser.h"
#include "DBHandler.h"

using namespace std;
using namespace _3gbridgeParser;

/**
 * The new status of a meta-job is the function of these parameters. */
struct MJStats
{
	size_t count,required, succAt, err, finished;
	string errorMsg;
};

/**
 * Plugin for 3g-bridge: Handles meta-jobs. */
class MetajobHandler : public GridHandler
{
public:
	/**
	 * Creates a MetajobHandler instance.
	 * @param config configuration
	 * @param instance TODO: ???
	 */
	MetajobHandler(GKeyFile *config, const char *instance)
		throw (BackendException *);
	~MetajobHandler() throw () {}

	/**
	 * Submit jobs. Submits a vector of jobs to EGEE.
	 * @param jobs jobs to submit
	 */
	void submitJobs(JobVector &jobs) throw (BackendException *);

	/**
	 * Update status of jobs. Updates status of jobs belonging to the
	 * plugin instance.
	 */
	void updateStatus(void) throw (BackendException *);

	/**
	 * Handle a given job. DBHandler uses this function to perform
	 * different operations on a job.
	 * @param job the job to handle
	 */
	void poll(Job *job) throw (BackendException *);

private:
	///////////////////////////////////
	// Parsing (submitting) meta-jobs
	///////////////////////////////////

	/**
	 * Used to implement transactioning in parser iterations */
	DBHWrapper *dbh;
	/**
	 * Output stream for mapping-file. Created by submitJobs and used by
	 * qJobHandler */
	ofstream *mappingFile;
	
	/**
	 * Delegate function. Called by parseMetaJob() to create jobs in the
	 * database using jd as a template.
	 * @see queueJobHandler */
	static void qJobHandler(MetajobHandler *instance,
				_3gbridgeParser::JobDef &jd,
				size_t count);
	/**
	 * Create input for parseMetaJob() from a Job object. */
	static void translateJob(Job const *job, MetaJobDef &mjd, JobDef &jd,
				 string &mjfileName)
		throw (BackendException*);
	/**
	 * Parse data stored in gridId of meta-job. */
	static void getExtraData(const string &data,
				 string &grid,  size_t &count, size_t &startLine,
				 string &reqd, string &succAt, string const &jobId)
		throw (BackendException*);

	/////////////////////////////////////////
	// Status polling and sub-job processing
	/////////////////////////////////////////
	
	/**
	 * Gets statistics about a meta-job's sub-jobs */
	auto_ptr<MJStats> getMetajobStatusInfo(
		const Job *job, DBHWrapper &dbh) const;
	/**
	 * Updates the status of the meta-job using statistics about sub-jobs */
	void updateJobStatus(
		Job *job, DBHWrapper &dbh, MJStats *stats,
		bool *updateNeeded, bool *cleanupNeeded) const;
	/**
	 * Updates the statistics file if found to be neccesary */
	void updateStatsFileIfNeccesary(
		Job *job, DBHWrapper &dbh, const MJStats &stats,
		bool updateNeeded) const;
	/**
	 * Cancels the meta-job and its sub-jobs */
	void userCancel(Job* job) const;
	/**
	 * Process sub-jobs' outputs and delete them. */
	void processFinishedSubjobsOutputs(DBHWrapper &dbh, Job *job) const;
	/**
	 * Delete the job record  */
	void deleteJob(Job *job) const;
	/**
	 * Remove files from the disk */
	void cleanupJob(Job *job) const;
	/**
	 * Clean up everyrthing after sub-jobs and delete them from the
	 * database. */
	void cleanupSubjobs(DBHWrapper &dbh, Job *metajob) const;

	/////////////////////////////
	// Creating archive output
	/////////////////////////////

	/**
	 * Create full path for the given archive file */
	string archFile(const Job *job, CSTR_C basename) const;
	/**
	 * Determines if the archiving process has been started and is still
	 * running. */
	bool isArchivingProcessRunning(const Job *job) const;
	/**
	 * Determines if the archving process has been finsihed (either way). */
	bool isArchivingProcessFinished(const Job *job) const;
	/**
	 * Determines if the archiving process has been _successfully_
	 * finished. */
	bool isArchivingSuccessful(const Job *job) const;
	/**
	 * Returnes the error message if the archiving process has
	 * failed. Returns empty string if there's been no error. */
	string archivingError(const Job *job) const;
	/**
	 * Creates the neccesary script and starts the archiving process.  */
	void startArchivingProcess(const Job *job) const;
	/**
	 * Cleans up after archiving. */
	void cleanupArchiving(const Job *job) const;

	//////////////////
	// Configuration
	//////////////////

	/**
	 * From configuration: Max number of jobs to generate in an
	 * iteration. */
	size_t maxJobsAtOnce;
	/**
	 * From configuration: Minimum time (in seconds) to elapse between
	 * updates sub-jobs' status report */
	size_t minElapse;
	/**
	 * From configuration: Output base directory. */
	string outDirBase;
	/**
	 * From configuration: Input base directory. */
	string inDirBase;
	/**
	 * From configuration: Output base URL */
	string outURLBase;
};


#endif //METAJOB_HANDLER_H
