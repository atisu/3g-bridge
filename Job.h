/*
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

#ifndef JOB_H
#define JOB_H

#include "AlgQueue.h"
#include "FileRef.h"

#include <memory>
#include <string>
#include <vector>
#include <map>

using namespace std;


/**
 * Class of job informations. The Job class holds every information related to
 * an entry in the cg_job table.
 */
class Job {
public:
	/**
	 * Enumerator of job statuses.
	 * The JobStatus enumerates the possible job status values.
	 */
	enum JobStatus {
		PREPARE,	/**< Enum value for the PREPARE status. */
		INIT,		/**< Enum value for the INIT status. */
		RUNNING,	/**< Enum value for the RUNNING status. */
		FINISHED,	/**< Enum value for the FINISHED status. */
		ERROR,		/**< Enum value for the ERROR status. */
		TEMPFAILED,	/**< Enum value for the TEMPFAILED status. */
		CANCEL		/**< Enum value for the CANCEL status. */
	};

	/**
	 * The Job constructor. Creates a Job object.
	 * @param id the Job's unique identifier.
	 * @param name the name of the executable.
	 * @param grid destination grid name to use.
	 * @param args command-line arguments.
	 * @param status status of the job.
	 * @see id()
	 * @see name()
	 * @see args()
	 * @see grid()
	 * @see status()
	 */
	Job(const char *id, const char *name, const char *grid, const char *args, JobStatus status, const vector<string> *env = NULL);

	/**
	 * Empty constructor.
	 */
	Job() {};

	/**
	 * The Job destructor.
	 */
	~Job() {};

	/**
	 * Return the Job's unique identifier.
	 * @see id()
	 * @return The Job's unique identifier
	 */
	const string &getId() const { return id; }

	/**
	 * Return the Job's name.
	 * @see name()
	 * @return The Job's name
	 */
	const string &getName() const { return name; }

	/**
	 * Return the Job's grid.
	 * @see grid()
	 * @return The Job's grid
	 */
	const string &getGrid() const { return grid; }

	/**
	 * Return the Job arguments.
	 * @see args()
	 * @return The Job's arguments
	 */
	const string &getArgs() const { return args; }

	/**
	 * Return the Job's algorithm queue.
	 * @see algQ()
	 * @see AlgQueue()
	 * @return The Job's algorithm queue
	 */
	AlgQueue *getAlgQueue() { return algQ; }

	/**
	 * Add an input file to the job.
	 * @param localname name of the file as the Job tries to open it
	 * @param fsyspath location of the file on the filesystem
	 * @see inputs()
	 */
	void addInput(const string &localname, const FileRef &fileref);

	/**
	 * Return vector of input files. Returns a vector of the localnames
	 * belonging to the input files.
	 * @see inputs()
	 * @return vector of localnames
	 */
	auto_ptr< vector<string> > getInputs() const;

	/**
	 * Get file reference for a file.
	 * @param localname the localname we're interested in
	 * @see inputs()
	 * @return the file reference
	 */
	const FileRef &getInputRef(const string localname) { return inputs[localname]; }

	/**
	 * Get the location of a file on the filesystem.
	 * @param localname the localname we're interested in
	 * @see inputs()
	 * @return the filesystem location of localname
	 */
	const string &getInputPath(const string localname) { return inputs[localname].getURL(); }

	/**
	 * Add an output file to the job.
	 * @param localname name of the file as the Job tries to open it
	 * @param fsyspath expected location of the file on the filesystem
	 * @see outputs()
	 */
	void addOutput(const string &localname, const string &fsyspath);

	/**
	 * Return vector of output files. Returns a vector of the localnames
	 * belonging to the output files.
	 * @see outputs()
	 * @return vector of localnames
	 */
	auto_ptr< vector<string> > getOutputs() const;

	/**
	 * Get the expected location of a file on the filesystem.
	 * @param localname the localname we're interested in
	 * @see outputs()
	 * @return the expected filesystem location of localname
	 */
	const string &getOutputPath(const string localname) { return outputs[localname]; }

	/**
	 * Set the Job's grid identifier.
	 * @param sId the grid identifier to set
	 * @see gridId()
	 */
	void setGridId(const string &sId);

	/**
	 * Get the Job's grid identifier.
	 * @see gridId()
	 * @return the Job's grid identifier
	 */
	const string &getGridId() const { return gridId; }

	/**
	 * Set the Job's grid data.
	 * @param sData the grid data to set
	 * @see gridData()
	 */
	void setGridData(const string &sData);

	/**
	 * Get the Job's grid data.
	 * @see gridData()
	 * @return the Job's grid data
	 */
	const string &getGridData() const { return gridData; }

	/**
	 * Set the Job's tag.
	 * @param sTag the tag to set
	 * @see tag()
	 */
	void setTag(const string &sTag);

	/**
	 * Get the Job's tag.
	 * @see tag()
	 * @return pointer to the Job's tag or NULL if tag hasn't been set
	 */
	const string *getTag() const { return tag; }

	/**
	 * Set the Job's status.
	 * @param nStat the status to set
	 * @param updateDB indicates that the job's DB status should be updated
	 *        as well
	 * @see status()
	 */
	void setStatus(JobStatus nStat, bool updateDB = true);

	/**
	 * Get the Job's status.
	 * @see status()
	 * @return the Job's status
	 */
	JobStatus getStatus() const { return status; }

	/**
	 * Delete the Job from the database. Removes the entry belonging to
	 * the Job from the database.
	 */
	void deleteJob();

	/**
	 * Get list of environment variables.
	 */
	auto_ptr< vector<string> > getEnvs() const;

	/**
	 * Get value of an environment variable.
	 */
	const string &getEnv(const string &name) { return envs[name]; }

	/**
	 * Set value of an environment variable.
	 */
	void addEnv(const string &name, const string &value) { envs[name] = value; }

private:
	/// The Job's unique identifier
	string id;

	/// The Job executables's name
	string name;

	/// Command-line arguments of the Job
	string args;

	/// The Job's destination grid
	string grid;

	/// The Job's grid identifier
	string gridId;

	/// The Job's grid data
	string gridData;

	/// The Job's tag
	string *tag;

	/// Algorithm queue the Job belongs to
	AlgQueue *algQ;

	/**
	 * Input files belonging to the Job. The variable is a map from strings
	 * to strings, where the map key is the localname of the input files,
	 * and the value belonging to a key is the location of the file on the
	 * filesystem.
	 */
	map<string, FileRef> inputs;

	/**
	 * Output files belonging to the Job. The variable is a map from
	 * strings to strings, where the map key is the localname of the output
	 * files, and the value belonging to a key is the expected location of
	 * the file on the filesystem.
	 */
	map<string, string> outputs;

	/**
	 * Environment variables assigned to the job.
	 */
	map<string, string> envs;

	/// The Job's status.
	JobStatus status;
};


/// The JobVector class used to hold a vector of Job objects.
class JobVector: public vector<Job *> {
public:
	/// Initialize the JobVector object.
	JobVector() { vector<Job *>(); }

	/**
	 * Destroy the JobVector object. Clears the vector of Job objects.
	 */
	~JobVector() { clear(); }

	/// Clear the vector of Job objects.
	void clear();
};

#endif /* JOB_H */
