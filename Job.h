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
 * Class of job informations. The Job class holds every information related to a
 * job in the 3G Bridge: the identifier, the application's name, the destination
 * plugin's name, the command-line arguments, the identifier in the destination
 * grid, destination-related data, set of input files, set of output files, tag,
 * environment variables, and the job's status.
 *
 * @author Gábor Gombás <gombasg@sztaki.hu>
 * @author Zoltán Farkas <zfarkas@sztaki.hu>
 */
class Job {
	//Common code for different constructors
	void init(const char *name, const char *grid,
			 const char *args,
			 const vector<string> *env);

public:
	/**
	 * Enumerator of job statuses.
	 * The JobStatus enumerates the possible job status values.
	 */
	enum JobStatus {
		PREPARE,	/**< Enum value for the PREPARE status, used to
				     indicate that the job is in its preparation
				     phase. */
		INIT,		/**< Enum value for the INIT status, used to
				     indicate that the job is ready for
				     execution. */
		RUNNING,	/**< Enum value for the RUNNING status, used to
				     indicate that the job is running. */
		FINISHED,	/**< Enum value for the FINISHED status, used to
				     indicate that the job has finished, and
				     results have been fetched. */
		ERROR,		/**< Enum value for the ERROR status, used to
				     indicate that some error has occured. */
		TEMPFAILED,	/**< Enum value for the TEMPFAILED status, used
				     to indicate that the job has failed due to
				     a temorary error. */
		CANCEL		/**< Enum value for the CANCEL status, used to
				     indicate the user's intention to cancel the
				     execution of the job, and to remove the
				     job. */
	};

	/**
	 * Constructor of a Job object.
	 * This constructor is used to create an initial Job object by filling
	 * in the main attributes: the job's identifier, the application's name,
	 * the destination grid, the command-line arguments, the job's status,
	 * and possible environment variables. Other properties have to be set
	 * using the relevant function.
	 * @param id the job's unique identifier.
	 * @param name the name of the executable/application.
	 * @param grid destination grid/plugin name to use.
	 * @param args command-line arguments.
	 * @param status status of the job.
	 * @param env environment variables for the job.
	 * @see id
	 * @see name
	 * @see args
	 * @see grid
	 * @see status
	 * @see envs
	 */
	Job(const char *id, const char *name, const char *grid, const char *args, JobStatus status, const vector<string> *env = NULL);

	/**
	 * Constructor of a Job object.
	 * This constructor is used to create an initial Job object by filling
	 * in the main attributes: the job's identifier, the application's name,
	 * the destination grid, the command-line arguments, the job's status,
	 * and possible environment variables. Other properties have to be set
	 * using the relevant function.
	 * @param id the job's unique identifier.
	 * @param metajobid the id of the parent meta-job, can be null
	 * @param name the name of the executable/application.
	 * @param grid destination grid/plugin name to use.
	 * @param args command-line arguments.
	 * @param status status of the job.
	 * @param env environment variables for the job.
	 * @see id
	 * @see name
	 * @see args
	 * @see grid
	 * @see status
	 * @see envs
	 */
	Job(const char *id, const char *metajobid, const char *name, const char *grid, const char *args, JobStatus status, const vector<string> *env = NULL);

	/**
	 * Default constructor.
	 * Default empty constructor for the Job class.
	 */
	Job() {};

	/**
	 * Destructor
	 * Destructor for the Job class.
	 */
	~Job();

	/**
	 * Get the job's unique identifier.
	 * This function returns the job's unique internal identifier.
	 * @see id
	 * @return the job's unique identifier
	 */
	const string &getId() const { return id; }

	/**
	 * Get the job's application/executable.
	 * This function returns the application (or executable) name belonging
	 * to the job.
	 * @see name
	 * @return application's/executable's name
	 */
	const string &getName() const { return name; }

	/**
	 * Get the job's destination grid/plugin
	 * This function returns the destination grid/plugin of the job.
	 * @see grid()
	 * @return The job's grid
	 */
	const string &getGrid() const { return grid; }

	/**
	 * Get the job's command-line arguments.
	 * This function returns the command-line arguments assigned to the job
	 * or NULL if no command-line arguments have been set.
	 * @see args
	 * @return the Job's command-line arguments
	 */
	const string &getArgs() const { return args; }

	/**
	 * Get the job's algorithm queue.
	 * This function returns the pointer to the algorithm queue the job
	 * belongs to.
	 * @see algQ
	 * @see AlgQueue
	 * @return the Job's algorithm queue
	 */
	AlgQueue *getAlgQueue() { return algQ; }

	/**
	 * Add an input file reference to the job.
	 * This function can be used to add an input file reference to the job
	 * through a FileRef object.
	 * @see inputs
	 * @see FileRef
	 * @param localname name of the file as the job tries to open it
	 * @param fileref the file reference belonging to the file
	 */
	void addInput(const string &localname, const FileRef &fileref);

	/**
	 * Get list of input files.
	 * This function returns a vector of the input file local names
	 * belonging to the job
	 * @see inputs
	 * @return vector of input file local names
	 */
	auto_ptr< vector<string> > getInputs() const;

	/**
	 * Get file reference for an input file
	 * This function returns the file reference object belonging to an input
	 * file.
	 * @see inputs
	 * @see FileRef
	 * @param localname the local name to get the reference for
	 * @return the file reference belonging to the local name
	 */
	const FileRef &getInputRef(const string localname) { return inputs[localname]; }

	/**
	 * Get all file reference info at once.
	 * @see inputs
	 * @see FileRef
	 * @return A maping of logical names to input file references.
	 */
	const map<string, FileRef> &getInputRefs() const { return inputs; }

	/**
	 * Add an output file to the job.
	 * This function adds an output file to the job.
	 * @see outputs
	 * @param localname name of the file as the job tries to open it
	 * @param fsyspath expected location of the file on the filesystem
	 */
	void addOutput(const string &localname, const string &fsyspath);

	/**
	 * Get list of output files.
	 * This function returns a vector of the output file local names
	 * belonging to the job.
	 * @see outputs
	 * @return vector of output file local names
	 */
	auto_ptr< vector<string> > getOutputs() const;

	/**
	 * Get the expected location of a file on the filesystem.
	 * This function returns the expected path of and output file on the
	 * filesystem.
	 * @see outputs
	 * @param localname the local name to get the path for
	 * @return the expected filesystem location of the output file
	 */
	const string &getOutputPath(const string localname) { return outputs[localname]; }

	const map<string, string> &getOutputMap() const { return outputs; }

	/**
	 * Set the job's grid identifier.
	 * This function sets the job's grid identifier.
	 * @see gridId
	 * @param sId the grid identifier to set
	 */
	void setGridId(const string &sId);

	/**
	 * Get the job's grid identifier.
	 * This function returns the job's current grid identifier.
	 * @see gridId
	 * @return the job's grid identifier
	 */
	const string &getGridId() const { return gridId; }

	/**
	 * Set the job's grid data.
	 * This function sets the job's grid data.
	 * @see gridData
	 * @param sData the grid data to set
	 */
	void setGridData(const string &sData);

	/**
	 * Get the job's grid data.
	 * This function returns the job's current grid data.
	 * @see gridData
	 * @return the job's grid data
	 */
	const string &getGridData() const { return gridData; }

	/**
	 * Sets the id of the parent meta-job.
	 * @see metajobid
	 * @param mjId the meta-job id to set
	 */
	void setMetajobId(const string &mjId);

	/**
	 * Get the id of the parent meta-job (if any).
	 * @see metajobid
	 * @return the id of the parent meta-job
	 */
	const string &getMetajobId() const { return metajobid; }

	/**
	 * Set the job's tag.
	 * This function sets the job's tag.
	 * @see tag
	 * @param sTag the tag to set
	 */
	void setTag(const string &sTag);

	/**
	 * Get the job's tag.
	 * This function returns the job's current tag
	 * @see tag
	 * @return pointer to the job's tag or NULL if tag hasn't been set
	 */
	const string *getTag() const { return tag; }

	/**
	 * Set the job's status.
	 * This function sets the job's status, and optionally also performs the
	 * database update.
	 * @see status
	 * @param nStat the status to set
	 * @param updateDB indicates that the job's DB status should be updated
	 *        as well
	 */
	void setStatus(JobStatus nStat, bool updateDB = true);

	/**
	 * Get the job's status.
	 * This function returns the job's current status.
	 * @see status
	 * @return the job's status
	 */
	JobStatus getStatus() const { return status; }

	/**
	 * Delete the Job from the database.
	 * This function removes the entry belonging to the job from the
	 * database.
	 */
	void deleteJob();

	/**
	 * Get list of environment variables.
	 * This function returns the list of environment variables set for the
	 * job.
	 * @see envs
	 * @return vector of defined environment variables
	 */
	auto_ptr< vector<string> > getEnvs() const;

	/**
	 * Get value of an environment variable.
	 * This function returns the value of an environment variable.
	 * @see envs
	 * @param name the name of the environment variable to get
	 * @return value of the specified environment variable
	 */
	const string &getEnv(const string &name) { return envs[name]; }

	/**
	 * Set value of an environment variable.
	 * This function sets the value of an environment variable.
	 * @see envs
	 * @param name the environment variable to set
	 * @param value the value to use
	 */
	void addEnv(const string &name, const string &value) { envs[name] = value; }

private:
	/// The job's unique identifier.
	string id;

	/// The id of the parent meta-job (if any) 
	string metajobid;

	/// The executable/application name belonging to the job.
	string name;

	/// The destination grid/plugin of the job.
	string grid;

	/// Command-line arguments of the job.
	string args;

	/// The grid identifier of the job.
	string gridId;

	/// The grid data of the job.
	string gridData;

	/// The tag of the job.
	string *tag;

	/// The Algorithm queue the job belongs to.
	AlgQueue *algQ;

	/**
	 * Input files belonging to the Job.
	 * The variable is a map from strings to strings, where the map key is
	 * the localname of the input files, and the value belonging to a key is
	 * the file reference describing properties of the file.
	 * @see FileRef
	 */
	map<string, FileRef> inputs;

	/**
	 * Output files belonging to the Job.
	 * The variable is a map from strings to strings, where the map key is
	 * the localname of the output files, and the value belonging to a key
	 * is the expected location of the file on the filesystem.
	 */
	map<string, string> outputs;

	/**
	 * Environment variables assigned to the job.
	 * The variable is a map from strings to strings, where the map key is
	 * the name of the environment variable, and the value belonging to a
	 * key is the value of the given environment variable.
	 */
	map<string, string> envs;

	/// Status of the job.
	JobStatus status;
};


/**
 * The JobVector class used to hold a vector of Job objects.
 * The JobVector class is based on the std::vector<Job> class.
 */
class JobVector: public vector<Job *> {
public:
	/// Initialize the JobVector object.
	JobVector() { vector<Job *>(); }

	/**
	 * JobVector destructor.
	 * Destroy the JobVector object. Clears the vector of Job objects.
	 */
	~JobVector() { clear(); }

	/// Clear the vector of Job objects.
	void clear();
};

#endif /* JOB_H */
