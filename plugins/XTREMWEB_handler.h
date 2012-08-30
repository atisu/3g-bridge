#ifndef XWHANDLER_H
#define XWHANDLER_H

#include "Job.h"
#include "GridHandler.h"

#include <string>
#include <vector>
#include <openssl/ssl.h>


using namespace std;


/**
 * XtremWeb handler plugin.
 * Objects of this plugin manage execution of jobs in the XW infrastructure.
 */
class XWHandler : public GridHandler {
  
  public:
    /**
     * Constructor :  Initialize a plugin instance. Uses the configuration
     * file and the provided instance name to initialize.
     * @param config    Configuration file data
     * @param instance  Name of the plugin instance
     */
    XWHandler(GKeyFile * config, const char * instance)
      throw (BackendException *);
    
    
    /// Destructor
    ~XWHandler();
    
    
    /**
     * Submit jobs :  Submit a vector of bridge jobs to XtremWeb.
     * @param jobs  Vector of pointers to bridge jobs to submit
     */
    void submitJobs(JobVector & jobs) throw (BackendException *);
    
    
    /**
     * Update status of bridge jobs :
     * Request DBHandler::pollJobs() to update the status of all jobs
     * whose current bridge status is RUNNING or CANCEL.
     */
    void updateStatus(void) throw (BackendException *);
    
    
    /**
     * Poll the status of a given bridge job :
     * This is used as a callback for DBHandler::pollJobs().
     * Depending on the bridge status of the job :
     * - CANCEL :
     *   Destroy the corresponding XtremWeb job.
     * - RUNNING :
     *   Query XtremWeb for the status of the corresponding XtremWeb job.
     *   If the XtremWeb job is COMPLETED, retrieve the XtremWeb results.
     *   Update the status of the bridge job accordingly.
     * @param job  Pointer to the bridge job to poll
     */
    void poll(Job * job) throw (BackendException *);
    
    
  private:
    
    char *    g_xw_https_server;            // XtremWeb-HEP HTTPS server
    char *    g_xw_https_port;              // XtremWeb-HEP HTTPS port
    char *    g_xw_user;                    // XtremWeb-HEP user login
    char *    g_xw_password;                // XtremWeb-HEP user password
    int       g_xw_socket_fd;               // Socket to XtremWeb-HEP server
    SSL_CTX * g_ssl_context;                // SSL context
    SSL *     g_ssl_xw;                     // SSL object for XtremWeb-HEP server
    string    g_xw_client_bin_folder_str;   // XtremWeb-HEP client binaries folder
    string    g_xw_files_folder_str;        // Folder for XW input + output files
    bool      g_xwclean_forced;             // Forced 'xwclean' before 'xwstatus'
    int       g_sleep_time_before_download; // Sleeping time before download in s
    string    g_xw_apps_message_str;        // XW message listing XW applications
    
    /**
     * Update the status of a given bridge job :
     * - From the XtremWeb status of the job, calculate the new bridge status.
     * - Store the new bridge status and the XtremWeb message in the database.
     * @param job            Pointer to the bridge job to update
     * @param xw_job_status  XtremWeb status of the job
     * @param xw_message     XtremWeb message associated to the job
     */
    void updateJobStatus(Job *          job,
                         const string & xw_job_status,
                         const string & xw_message);
    
};

#endif /* XWHANDLER_H */
