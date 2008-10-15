#ifndef EGEEHANDLER_H
#define EGEEHANDLER_H

#include "Job.h"
#include "GridHandler.h"

#include <string>
#include <vector>
#include <glite/wms/wmproxyapi/wmproxy_api.h>
#include <globus_ftp_client.h>


using namespace std;
using namespace glite::wms;
using namespace glite::wms::wmproxyapi;


/**
 * EGEE handler plugin. Objects of this plugin manage execution of jobs in the
 * EGEE infrastructure.
 */
class EGEEHandler : public GridHandler {
    public:
	/**
	 * Constructor. Initialize a plugin instance. Uses the configuration
	 * file and the provided instance name to initialize.
	 * @param config configuration file data
	 * @param instance name of the plugin instance
	 */
	EGEEHandler(GKeyFile *config, const char *instance) throw (BackendException *);

	/// Destructor
	~EGEEHandler();

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

	/**
	 * Get an instance of the plugin. Creates a new instance of the EGEE
	 * plugin.
	 * @param config configuration file data
	 * @param instance name of the plugin instance
	 * @return pointer to the plugin
	 */
	static GridHandler *getInstance(GKeyFile *config, const char *instance);

    private:
	/**
	 * Update status of jobs.
	 * @param jobs vector of jobs to update
	 */
	void getStatus(JobVector &jobs) throw (BackendException *);

	/// Buffer size for GSIFTP operations
	static const int GSIFTP_BSIZE = 1024000;

	/// Success indicator constant
	static const int SUCCESS = 0;

	/// Lock for GSIFTP operations
	static globus_mutex_t lock;

	/// Conditional for GSIFTP operations
	static globus_cond_t cond;

	/// Finish indicator for GSIFTP operations
	static globus_bool_t done;

	/// Error indicator for GSIFTP operations
	static bool globus_err;

	/// Offset for GSIFTP operations
	static int global_offset;

	/// Temporary directory for storing proxy
	string tmpdir;

	/// WMProxy endpoint
	char *wmpendp;

	/// Hostname of MyProxy
	char *myproxy_host;

	/// Port of MyProxy
	char *myproxy_port;

	/// Username for MyProxy
	char *myproxy_user;

	/// Password for MyProxy
	char *myproxy_pass;

	/// ConfigContext for EGEE operations
	ConfigContext *cfg;

	/// Name of the supported Virtual Organization
	char *voname;

	/**
	 * Initialize GSIFTP for operations. Initializes different structures
	 * for GSIFTP operations.
	 * @param ftp_handle GSIFTP client handle pointer
	 * @param ftp_handle_attrs GSIFTP client handle attributes pointer
	 * @param ftp_op_attrs GSIFTP client operation attributes pointer
	 */
	void init_ftp_client(globus_ftp_client_handle_t *ftp_handle, globus_ftp_client_handleattr_t *ftp_handle_attrs, globus_ftp_client_operationattr_t *ftp_op_attrs);

	/**
	 * Destroy structures of GSIFTP operations.
	 * @param ftp_handle GSIFTP client handle pointer
	 * @param ftp_handle_attrs GSIFTP client handle attributes pointer
	 * @param ftp_op_attrs GSIFTP client operation attributes pointer
	 */
	void destroy_ftp_client(globus_ftp_client_handle_t *ftp_handle, globus_ftp_client_handleattr_t *ftp_handle_attrs, globus_ftp_client_operationattr_t *ftp_op_attrs);

	/**
	 * Handle finish of GSIFTP operations. This function is called every
	 * time a GSIFTP operation has been finished.
	 * @param user_args pointer to user-provided arguments, ignored
	 * @param ftp_handle handle of GSIFTP
	 * @param error pointer to GSIFTP error structure
	 */
	static void handle_finish(void *user_args, globus_ftp_client_handle_t *ftp_handle, globus_object_t *error);

	/**
	 * Handle GSIFTP data write. This function is called every time data
	 * should be send over a GSIFTP connection.
	 * @param user_args FILE* variable of the file to read from the data
	 * @param handle handle of GSIFTP
	 * @param error pointer to GSIFTP error structure
	 * @param buffer pointer to buffer where data should be read in
	 * @param buflen number of bytes read into buffer
	 * @param offset offset in the file read
	 * @param eof indicator of end of file
	 */
	static void handle_data_write(void *user_args, globus_ftp_client_handle_t *handle, globus_object_t *error, globus_byte_t *buffer, globus_size_t buflen, globus_off_t offset, globus_bool_t eof);

	/**
	 * Handle GSIFTP data read. This function is called every time data
	 * is read from a GSIFTP connection and should be written to a file.
	 * @param user_args FILE* variable of the file to write the data to
	 * @param ftp_handle handle of GSIFTP
	 * @param error pointer to GSIFTP error structure
	 * @param buffer pointer to buffer where received data is
	 * @param buflen number of bytes in the buffer
	 * @param offset offset in the file write, ignored
	 * @param eof indicator of end of file, ignored
	 */
	static void handle_data_read(void *user_args, globus_ftp_client_handle_t *ftp_handle, globus_object_t *error, globus_byte_t *buffer, globus_size_t buflen, globus_off_t offset, globus_bool_t eof);

	/**
	 * Upload files using GSIFTP operations.
	 * @param inFiles vector of input file locations to upload
	 * @param destURI destination URI where the files should be uploaded
	 */
	void upload_file_globus(const vector<string> &inFiles, const string &destURI);

	/**
	 * Download files using GSIFTP operations.
	 * @param remFiles vector of remote file URIs to download
	 * @param locFiles destination of files on the filesystem
	 */
	void download_file_globus(const vector<string> &remFiles, const vector<string> &locFiles);

	/**
	 * Remove files using GSIFTP operations. Each file receives the prefix
	 * before the removal is issued.
	 * @param fileNames the files to remove
	 * @param prefix the prefix to add (optional)
	 */
	void delete_file_globus(const vector<string> &fileNames, const string &prefix = "");

	/**
	 * Clean an EGEE job. The operation purges the job using the jobPurge
	 * function provided by EGEE API.
	 * @param jobID the job identifier to clean
	 */
	void cleanJob(const string &jobID);

	/**
	 * Delegate a proxy.
	 * @param delID the delegation identifier
	 */
	void delegate_Proxy(const string& delID);

	/**
	 * Throw an exception using a BaseException.
	 * @param func the function name where the exception occured
	 * @param e the BaseException to use
	 */
	void throwStrExc(const char *func, const BaseException &e) throw(BackendException *);

	/**
	 * Throw an exception using a message string.
	 * @param func the function name where the exception occured
	 * @param str the message to throw
	 */
	void throwStrExc(const char *func, const string &str) throw(BackendException *);

	/**
	 * Renew proxy file. This function is periodically called, and gets
	 * a new proxy every time the existing proxy's lifetime is below 18
	 * hours using the MyProxy informations provided in the configuration
	 * file
	 */
	void renew_proxy();

	/**
	 * Get output of a job. The function downloads output files produced by
	 * the job, removes and remote file used by the job, and finally cleans
	 * the job.
	 * @param jobs the job to use
	 */
	void getOutputs_real(Job *jobs);

	/**
	 * Create an EGEE ConfigContext used by WMS/L&B operations.
	 */
	void createCFG();

	/**
	 * Get proxy informations. The function reads a proxy file, and returns
	 * its subject and remaining lifetime.
	 * @param proxyfile location of the proxy file
	 * @param[out] lifetime remaining lifetime of the proxy
	 * @return subject string of the proxy
	 */
	char *getProxyInfo(const char *proxyfile, time_t *lifetime);

	/**
	 * Update an EGEE job's status. The function uses EGEE L&B API
	 * functions to get the status of the job. The status info is
	 * also updated in the database.
	 * @param job the job to use
	 */
	void updateJob(Job *job);

	/**
	 * Cancel an EGEE job. The function uses EGEE API functions to cancel
	 * the job. The job is also removed from the database.
	 * @param job the job to cancel
	 */
	void cancelJob(Job *job);
};

#endif /* EGEEHANDLER_H */
