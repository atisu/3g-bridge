#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Conf.h"
#include "DBHandler.h"
#include "DownloadManager.h"
#include "Job.h"
#include "Logging.h"

#include <string>

#include <stdlib.h>
#include <signal.h>

#include <openssl/crypto.h>
#include <curl/curl.h>
#include <uuid/uuid.h>

#include "soap/soapH.h"
#include "soap/G3BridgeBinding.nsmap"

#include <glib.h>

using namespace std;

/**********************************************************************
 * Global variables
 */

static char *download_dir;
static char *partial_dir;
static char *output_dir;
static char *output_url_prefix;

static volatile int finish;

GKeyFile *global_config = NULL;

static GThreadPool *soap_pool;

static DownloadManager *dlm;


/**********************************************************************
 * Schedule the download of an input file
 */

static void start_download(const string jobid, const string localName,
		const string URL, const string path)
{
	/* XXX */
}

/**********************************************************************
 * Calculate the file system location where an input file should be downloaded to
 */

static void calc_input_path(const string jobid, const string localName, string &path)
{
	/* XXX */
}

/**********************************************************************
 * Calculate the location where an output file will be stored
 */

static void calc_output_path(const string jobid, const string localName, string &path)
{
	/* XXX */
}

/**********************************************************************
 * Web service routines
 */

int G3BridgeOp__submit(struct soap*, G3BridgeType__JobList *jobs, struct G3BridgeOp__submitResponse &jobids)
{
	jobids.jobids = new G3BridgeType__JobIDList;
	for (vector<G3BridgeType__Job *>::const_iterator jobit = jobs->job.begin(); jobit != jobs->job.end(); jobit++)
	{
		uuid_t uuid;
		char jobid[37];

        	uuid_generate(uuid);
	        uuid_unparse(uuid, jobid);

		G3BridgeType__Job *wsjob = *jobit;
        	Job qmjob((const char *)jobid, wsjob->alg.c_str(), wsjob->grid.c_str(), wsjob->args.c_str(), Job::PREPARE);

		for (vector<G3BridgeType__LogicalFile *>::const_iterator inpit = wsjob->inputs.begin(); inpit != wsjob->inputs.end(); inpit++)
		{
			G3BridgeType__LogicalFile *lfn = *inpit;

			string path;
			calc_input_path(jobid, lfn->logicalName, path);
			qmjob.addInput(lfn->logicalName, path);

			start_download(jobid, lfn->logicalName, lfn->URL, path);
		}

		for (vector<string>::const_iterator outit = wsjob->outputs.begin(); outit != wsjob->outputs.end(); outit++)
		{
			string path;

			calc_output_path(jobid, *outit, path);
			qmjob.addOutput(*outit, path);
		}

		DBHandler *dbh = DBHandler::get();
		dbh->addJob(qmjob);
		DBHandler::put(dbh);
	}

	return SOAP_OK;
}


int G3BridgeOp__getStatus(struct soap*, G3BridgeType__JobIDList *jobids, struct G3BridgeOp__getStatusResponse &statuses)
{

	statuses.statuses = new G3BridgeType__StatusList();

        DBHandler *dbh = DBHandler::get();

	for (vector<string>::const_iterator it = jobids->jobid.begin(); it != jobids->jobid.end(); it++)
	{
		G3BridgeType__JobStatus status = G3BridgeType__JobStatus__UNKNOWN;
		Job job;

		try
		{
			dbh->getJob(job, *it);
			switch (job.getStatus())
			{
				case Job::PREPARE:
				case Job::INIT:
					status = G3BridgeType__JobStatus__INIT;
					break;
				case Job::RUNNING:
					status = G3BridgeType__JobStatus__RUNNING;
					break;
				case Job::FINISHED:
					status = G3BridgeType__JobStatus__FINISHED;
					break;
				case Job::ERROR:
				case Job::CANCEL:
					status = G3BridgeType__JobStatus__ERROR;
					break;
			}
		}
		catch (...)
		{
		}

		statuses.statuses->status.push_back(status);
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}


int G3BridgeOp__delJob(struct soap*, G3BridgeType__JobIDList *jobids, struct G3BridgeOp__delJobResponse &_param_3 G_GNUC_UNUSED)
{
        DBHandler *dbh = DBHandler::get();
	for (vector<string>::const_iterator it = jobids->jobid.begin(); it != jobids->jobid.end(); it++)
	{
		/* XXX Abort all pending transfers */
		/* XXX Delete input/output files */
		dbh->deleteJob(*it);
	}
	DBHandler::put(dbh);
	return SOAP_OK;
}


int G3BridgeOp__getOutput(struct soap*, G3BridgeType__JobIDList *jobids, struct G3BridgeOp__getOutputResponse &_param_4)
{
	/* XXX */
	return SOAP_OK;
}

/**********************************************************************
 * The SOAP thread handler
 */

static void soap_service_handler(void *data, void *user_data G_GNUC_UNUSED)
{
	struct soap *soap = (struct soap *)data;

	soap_serve(soap);
	soap_destroy(soap);
	soap_end(soap);
	soap_free(soap);
}

/**********************************************************************
 * The main program
 */

static void sigint_handler(int signal __attribute__((__unused__)))
{
	finish = true;
}

int main(int argc, char **argv)
{
	int port, ws_threads, dl_threads;
        struct soap soap;
	struct sigaction sa;

	Logging::init(cout, LOG_INFO);

	if (argc != 2)
	{
		cerr << "Usage: " << argv[0] << " <configfile>" << endl;
		exit(-1);
	}

	GError *error = NULL;
	global_config = g_key_file_new();
	g_key_file_load_from_file(global_config, argv[1], G_KEY_FILE_NONE, &error);
	if (error)
	{
		LOG(LOG_ERR, "Failed to load the config file: %s", error->message);
		g_error_free(error);
	        exit(1);
	}

	char *level = g_key_file_get_string(global_config, GROUP_DEFAULTS, "log-level", NULL);
	if (level)
	{
		Logging::init(cout, level);
		g_free(level);
	}

	port = g_key_file_get_integer(global_config, GROUP_WEBSERVICE, "port", &error);
	if (!port || error)
	{
		LOG(LOG_ERR, "Failed to retrieve the listener port: %s", error->message);
		g_error_free(error);
	        exit(1);
	}
	if (port <= 0 || port > 65535)
	{
		LOG(LOG_ERR, "Invalid port number (%d) specified", port);
		exit(-1);
	}

	dl_threads = g_key_file_get_integer(global_config, GROUP_WEBSERVICE, "download-threads", &error);
	if (!dl_threads || error)
	{
		LOG(LOG_ERR, "Failed to parse the number of download threads: %s", error->message);
		g_error_free(error);
	        exit(1);
	}
	if (dl_threads <= 0 || dl_threads > 1000)
	{
		LOG(LOG_ERR, "Invalid thread number (%d) specified", dl_threads);
		exit(-1);
	}

	ws_threads = g_key_file_get_integer(global_config, GROUP_WEBSERVICE, "service-threads", &error);
	if (!ws_threads || error)
	{
		LOG(LOG_ERR, "Failed to parse the number of service threads: %s", error->message);
		g_error_free(error);
	        exit(1);
	}
	if (ws_threads <= 0 || ws_threads > 1000)
	{
		LOG(LOG_ERR, "Invalid thread number (%d) specified", ws_threads);
		exit(-1);
	}

	download_dir = g_key_file_get_string(global_config, GROUP_WEBSERVICE, "download-dir", &error);
	if (!download_dir || error)
	{
		LOG(LOG_ERR, "Failed to get the download directory: %s", error->message);
		g_error_free(error);
	        exit(1);
	}

	partial_dir = g_key_file_get_string(global_config, GROUP_WEBSERVICE, "partial-dir", &error);
	if (!partial_dir || error)
	{
		LOG(LOG_ERR, "Failed to get the partial directory: %s", error->message);
		g_error_free(error);
	        exit(1);
	}

	output_dir = g_key_file_get_string(global_config, GROUP_WEBSERVICE, "output-dir", &error);
	if (!output_dir || error)
	{
		LOG(LOG_ERR, "Failed to get the output base directory: %s", error->message);
		g_error_free(error);
	        exit(1);
	}

	output_url_prefix = g_key_file_get_string(global_config, GROUP_WEBSERVICE, "output-url-prefix", &error);
	if (!output_url_prefix || error)
	{
		LOG(LOG_ERR, "Failed to get the output URL prefix: %s", error->message);
		g_error_free(error);
	        exit(1);
	}

	/* Set up the signal handlers */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);

	/* Initialize glib's thread system */
	g_thread_init(NULL);

	dlm = new DownloadManager(dl_threads);

	soap_init2(&soap, SOAP_IO_KEEPALIVE, SOAP_IO_KEEPALIVE | SOAP_IO_CHUNK);
        soap.send_timeout = 60;
	soap.recv_timeout = 60;
	soap.accept_timeout = 3600;
	soap.max_keep_alive = 100;

	SOAP_SOCKET ss = soap_bind(&soap, NULL, port, 100);
	if (!soap_valid_socket(ss))
	{
		/* XXX Use LOG() */
		soap_print_fault(&soap, stderr);
		exit(-1);
	}

	soap_pool = g_thread_pool_new(soap_service_handler, NULL, ws_threads, TRUE, NULL);
	if (!soap_pool)
	{
		LOG(LOG_ERR, "Failed to launch the WS threads");
		exit(1);
	}

        while (!finish)
        {
		soap.socket_flags = MSG_NOSIGNAL;

    		SOAP_SOCKET cs = soap_accept(&soap);
                if (!cs)
                {
                        soap_print_fault(&soap, stderr);
                        exit(-1);
		}
                struct soap *tsoap = soap_copy(&soap);
		g_thread_pool_push(soap_pool, tsoap, NULL);
	}

	soap_done(&soap);

	g_thread_pool_free(soap_pool, TRUE, TRUE);

	delete dlm;

	return 0;
}
