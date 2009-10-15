/*
 * Copyright (C) 2008 MTA SZTAKI LPDS
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
#ifndef EGEE_HANDLER_H
#define EGEE_HANDLER_H

#include "Job.h"
#include "GridHandler.h"

#include <string>
#include <vector>
#include <glite/wms/wmproxyapi/wmproxy_api.h>
#include <globus_ftp_client.h>
#include <globus_ftp_client_restart_plugin.h>

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

    private:

	/// Lock for GSIFTP operations
	static globus_mutex_t lock;

	/// Conditional for GSIFTP operations
	static globus_cond_t cond;

	/// Finish indicator for GSIFTP operations
	static globus_bool_t done;

	/// Error indicator for GSIFTP operations
	static bool globus_err;

	/// Error message of GSIFTP operations
	static char *globus_errmsg;

	/// Temporary directory for storing proxy
	string tmpdir;

	/// WMProxy endpoint
	char *wmpendp;

	/// Hostname of MyProxy
	char *myproxy_host;

	/// Port of MyProxy
	char *myproxy_port;

	/// User whose credentials are retrieved from MyProxy
	char *myproxy_user;

	/// Certificate for MyProxy authentication
	char *myproxy_authcert;

	/// Key for MyProxy authentication
	char *myproxy_authkey;

	/// Base GridFTP URL for storing inputsandbox files
	char *isb_url;

	/// ConfigContext for EGEE operations
	ConfigContext *cfg;

	/// Name of the supported Virtual Organization
	char *voname;

	/// Job logging directory
	char *joblogdir;

	/// Job logging level
	int jobloglevel;

	/**
	 * Initialize GSIFTP for operations. Initializes different structures
	 * for GSIFTP operations.
	 * @param ftp_handle GSIFTP client handle pointer
	 * @param ftp_handle_attrs GSIFTP client handle attributes pointer
	 * @param ftp_op_attrs GSIFTP client operation attributes pointer
	 */
	void init_ftp_client(globus_ftp_client_handle_t *ftp_handle, globus_ftp_client_handleattr_t *ftp_handle_attrs, globus_ftp_client_operationattr_t *ftp_op_attrs, globus_ftp_client_plugin_t *rst_pin = NULL);

	/**
	 * Destroy structures of GSIFTP operations.
	 * @param ftp_handle GSIFTP client handle pointer
	 * @param ftp_handle_attrs GSIFTP client handle attributes pointer
	 * @param ftp_op_attrs GSIFTP client operation attributes pointer
	 */
	void destroy_ftp_client(globus_ftp_client_handle_t *ftp_handle, globus_ftp_client_handleattr_t *ftp_handle_attrs, globus_ftp_client_operationattr_t *ftp_op_attrs, globus_ftp_client_plugin_t *rst_pin = NULL);

	/**
	 * Handle finish of GSIFTP operations. This function is called every
	 * time a GSIFTP operation has been finished.
	 * @param user_args pointer to user-provided arguments, ignored
	 * @param ftp_handle handle of GSIFTP
	 * @param error pointer to GSIFTP error structure
	 */
	static void handle_finish(void *user_args, globus_ftp_client_handle_t *ftp_handle, globus_object_t *error);

	/**
	 * Set Globus error message. globus_err is set to true, and globus_errmsg
	 * is filled up with the error message.
	 * @param error the error object to parse
	 */
	static void set_globus_err(globus_object_t *error);

	/**
	 * Transfer files using GASS operations.
	 * @param srcFiles vector of source file URLs to transfer
	 * @param dstFiles destination URLs where the files should be transferred
	 */
	void transfer_files_globus(const vector<string> &srcFiles, const vector<string> &dstFiles) throw(BackendException *);

	/**
	 * Remove files using GSIFTP operations. Each file receives the prefix
	 * before the removal is issued, and file basenames are used
	 * @param fileNames the files to remove
	 * @param prefix the prefix to add (optional)
	 */
	void delete_files_globus(const vector<string> &fileNames, const string &prefix);

	/**
	 * Create a directory using GSIFTP.
	 * @param the directory to create.
	 */
	void create_dir_globus(const string &dirurl) throw(BackendException *);

	/**
	 * Remove a directory using GSIFTP.
	 * @param the directory to remove.
	 */
	void remove_dir_globus(const string &dirurl);

	/**
	 * Clean an EGEE job. The operation purges the job using the jobPurge
	 * function provided by EGEE API.
	 * @param jobID the job identifier to clean
	 */
	void cleanJob(Job *job);

	/**
	 * Clean an EGEE job's remote storage. The function removes every file
	 * belonging to the job the from the job's storage URL.
	 * @param jobID the job to clean
	 */
	void cleanJobStorage(Job *job);

	/**
	 * Delegate a proxy.
	 * @param delID the delegation identifier
	 */
	void delegate_Proxy(const string& delID);

	/**
	 * Get EGEE exception message string.
	 * @param e the BaseException to use
	 * @return string representation of EGEE exception
	 */
	string getEGEEErrMsg(const BaseException &e);

	/**
	 * Renew proxy file. This function is periodically called, and gets
	 * a new proxy every time the existing proxy's lifetime is below 18
	 * hours using the MyProxy informations provided in the configuration
	 * file. (In MyProxy terms this is really a retrieval not renewal.)
	 */
	void renew_proxy() throw(BackendException *);

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
	void createCFG() throw(BackendException *);

	/**
	 * Get proxy informations. The function reads a proxy file, and returns
	 * its subject and remaining lifetime.
	 * @param proxyfile location of the proxy file
	 * @param[out] lifetime remaining lifetime of the proxy
	 */
	void getProxyInfo(const char *proxyfile, time_t *lifetime);

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
	 * @param clean boolean indicating if the job should be removed from
	 *	  the database or not
	 */
	void cancelJob(Job *job, bool clean = true);

	/**
	 * Log a message related to a job. Location of the log file is
	 * joblogdir + "/" + jobid + ".log". If level < jobloglevel, the
	 * message isn't printed.
	 * @param jobid the job ID to use
	 * @param level message log level: NONE, ERROR, ALL
	 * @param msg the message to log
	 */
	void logjob(const string& jobid, const int level, const string& msg);
};

#endif /* EGEE_HANDLER_H */
