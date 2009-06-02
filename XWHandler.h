#ifndef XWHANDLER_H
#define XWHANDLER_H

#include "Job.h"
#include "GridHandler.h"

#include <string>
#include <vector>


using namespace std;


/**
 * XW handler plugin. Objects of this plugin manage execution of jobs in the
 * XW infrastructure.
 */
class XWHandler : public GridHandler {
    public:
	/**
	 * Constructor. Initialize a plugin instance. Uses the configuration
	 * file and the provided instance name to initialize.
	 * @param config configuration file data
	 * @param instance name of the plugin instance
	 */
	XWHandler(GKeyFile *config, const char *instance) throw (BackendException *);

	/// Destructor
	~XWHandler();

	/**
	 * Submit jobs. Submits a vector of jobs to XW.
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
	void updateJob(Job *job, string status);
	/**
	 * update a given job. 
	 * @param job the job to handle
	 */
	void poll(Job *job) throw (BackendException *);

	/**
	 * Get an instance of the plugin. Creates a new instance of the XW
	 * plugin.
	 * @param config configuration file data
	 * @param instance name of the plugin instance
	 * @return pointer to the plugin
	 */
	static GridHandler *getInstance(GKeyFile *config, const char *instance);

    private:

	//TODO make this configurable of course ;-)
	string cmdbase;

	//path to the XtremWeb Client installation
	char* xwclient;

	//path to the nailgun executable
	char *ng;
	
	
};

#endif /* XWHANDLER_H */
