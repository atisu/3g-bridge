#ifndef GRIDHANDLER_H
#define GRIDHANDLER_H

#include "BackendException.h"
#include "Job.h"

#include <glib.h>

#include <vector>

using namespace std;

/**
 * GridHandler interface. Used by the Queue Manager to submit jobs, update
 * status of jobs and get output of finished jobs using grid functions.
 */
class GridHandler {
public:
	/// Initialize GridHandler.
	GridHandler() {	groupByNames = false; }

	/**
	 * Constructor using config file and instance name
	 * @param config the config file object
	 * @param instance the name of the instance
	 * @see name
	 */
        GridHandler(GKeyFile *config, const char *instance);

	/// Destructor
	virtual ~GridHandler() {}

	/**
	 * Return the name of the plugin instance.
	 * @see name
	 * @return instance name
	 */
        const char *getName(void) const { return name.c_str(); }

	/**
	 * Indicates wether the plugin groups jobs by algorithm names or not.
	 * @see groupByNames
	 * @return true if plugin groups algorithms by names
	 */
	bool schGroupByNames() const { return groupByNames; }

	/**
	 * Submit jobs in the argument.
	 * @param jobs JobVector of jobs to be submitted
	 */
        virtual void submitJobs(JobVector &jobs) throw (BackendException *) = 0;

	/// Update the status of previously submitted jobs in the database.
        virtual void updateStatus(void) throw (BackendException *) = 0;

        /**
         * Poll the status of a submitted job. This is used as a callback for
         * DBHandler::pollJobs().
	 * @param job the job to poll
         */
        virtual void poll(Job *job) throw (BackendException *) = 0;

protected:
	/**
	 * Indicates wether the GridHandler plugin is able to create batch
	 * packages of job using the same executable or not. Should be true
	 * for plugins that are able to create one grid job out of a number
	 * of jobs in the database (e.g. DC-API).
	 */
        bool groupByNames;

	/// Instance name of the GridHandler plugin
        string name;
};

#endif /* GRIDHANDLER_H */
