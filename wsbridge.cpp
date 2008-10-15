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
 * Calculate the file system location where an input file should be downloaded to
 */

static string calc_input_path(const string jobid, const string localName)
{
	/* XXX */
	return (string)download_dir + "/" + jobid + "_" + localName;
}

/**********************************************************************
 * Calculate the file system location where an input file should be downloaded to
 */

static string calc_temp_path(const string jobid, const string localName)
{
	/* XXX */
	return (string)partial_dir + "/" + jobid + "_" + localName;
}

/**********************************************************************
 * Calculate the location where an output file will be stored
 */

static string calc_output_path(const string jobid, const string localName)
{
	/* XXX */
	return (string)output_dir + "/" + jobid + "_" + localName;
}

/**********************************************************************
 * Database-aware wrapper for DLItem
 */

class DBItem: public DLItem
{
private:
	string jobId;
	string logicalFile;
public:
	DBItem(const string &jobId, const string &logicalFile, const string &URL);
	~DBItem();

	const string &getJobId() const { return jobId; };

	void finished();
	void failed();
};

DBItem::DBItem(const string &jobId, const string &logicalFile, const string &URL):
		DLItem(URL, calc_temp_path(jobId, logicalFile))
{
}

void DBItem::finished()
{
	auto_ptr<Job> job;

	DLItem::finished();

	DBHandler *dbh = DBHandler::get();
	dbh->deleteDL(jobId, logicalFile);
	job = dbh->getJob(jobId);
	DBHandler::put(dbh);

	/* If the job has already failed, just do not bother */
	if (job->getStatus() != Job::PREPARE)
	{
		failed();
		return;
	}

	string final_path = calc_input_path(jobId, logicalFile);
	int ret = rename(path.c_str(), final_path.c_str());
	if (ret)
	{
		LOG(LOG_ERR, "Failed to rename '%s' to '%s': %s",
			path.c_str(), final_path.c_str(), strerror(errno));
		failed();
		return;
	}

	/* Check if the job is now ready to be submitted */
	auto_ptr< vector<string> > inputs = job->getInputs();
	vector<string>::const_iterator it;
	for (it = inputs->begin(); it != inputs->end(); it++)
	{
		struct stat st;
		string path;

		path = calc_input_path(jobId, *it);
		ret = stat(path.c_str(), &st);
		if (!ret)
			break;
	}
	if (it == inputs->end())
	{
		job->setStatus(Job::INIT);
		/* XXX Send notification */
	}
}

void DBItem::failed()
{
	auto_ptr<Job> job;

	DLItem::failed();

	DBHandler *dbh = DBHandler::get();
	dbh->deleteDL(jobId, logicalFile);
	job = dbh->getJob(jobId);
	dbh->updateJobStat(jobId, Job::ERROR);
	DBHandler::put(dbh);

	/* Abort the download of the other input files */
	auto_ptr< vector<string> > inputs = job->getInputs();
	for (vector<string>::const_iterator it = inputs->begin(); it != inputs->end(); it++)
	{
		string path = calc_temp_path(jobId, *it);
		dlm->abort(path);
	}

	/* XXX Send notification */
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

		vector< pair<string, string> > inputs;

		G3BridgeType__Job *wsjob = *jobit;
        	Job qmjob((const char *)jobid, wsjob->alg.c_str(), wsjob->grid.c_str(), wsjob->args.c_str(), Job::PREPARE);

		for (vector<G3BridgeType__LogicalFile *>::const_iterator inpit = wsjob->inputs.begin(); inpit != wsjob->inputs.end(); inpit++)
		{
			G3BridgeType__LogicalFile *lfn = *inpit;

			string path = calc_input_path(jobid, lfn->logicalName);
			qmjob.addInput(lfn->logicalName, path);

			inputs.push_back(pair<string, string>(lfn->logicalName, lfn->URL));
		}

		for (vector<string>::const_iterator outit = wsjob->outputs.begin(); outit != wsjob->outputs.end(); outit++)
		{
			string path = calc_output_path(jobid, *outit);
			qmjob.addOutput(*outit, path);
		}

		DBHandler *dbh = DBHandler::get();
		dbh->addJob(qmjob);

		/* The downloads can only be started after the job record is in the DB */
		for (vector< pair<string,string> >::const_iterator it = inputs.begin(); it != inputs.end(); it++)
		{
			DBItem *item = new DBItem(qmjob.getId(), it->first, it->second);
			dbh->addDL(qmjob.getId(), it->first, it->second);
			dlm->add(item);
		}
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
		auto_ptr<Job> job;

		try
		{
			job = dbh->getJob(*it);
			switch (job->getStatus())
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
		auto_ptr<Job> job;

		job = dbh->getJob(*it);
		auto_ptr< vector<string> > files = job->getInputs();
		for (vector<string>::const_iterator fsit = files->begin(); fsit != files->end(); fsit++)
		{
			/* Abort the download if it is still in the queue */
			string path = calc_temp_path(*it, *fsit);
			dlm->abort(path);
			/* Delete the input file if it has been already downloaded */
			path = job->getInputPath(*fsit);
			unlink(path.c_str());
		}

		if (job->getStatus() == Job::RUNNING)
		{
			job->setStatus(Job::CANCEL);
			continue;
		}

		/* Delete the (possible) output files */
		files = job->getOutputs();
		for (vector<string>::const_iterator fsit = files->begin(); fsit != files->end(); fsit++)
		{
			string path = job->getOutputPath(*fsit);
			unlink(path.c_str());
		}

		/* Delete the job itself */
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

	dlm = new DownloadManager(dl_threads, 10);

	/* XXX Load the pending downloads from the database */

	soap_init2(&soap, SOAP_IO_KEEPALIVE, SOAP_IO_KEEPALIVE | SOAP_IO_CHUNK);
        soap.send_timeout = 60;
	soap.recv_timeout = 60;
	/* Give a small accept timeout to detect exit signals quickly */
	soap.accept_timeout = 1;
	soap.max_keep_alive = 100;
	soap.socket_flags = MSG_NOSIGNAL;

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
    		SOAP_SOCKET ret = soap_accept(&soap);
		if (ret == SOAP_INVALID_SOCKET)
		{
			if (!soap.errnum)
				continue;
                        soap_print_fault(&soap, stderr);
                        exit(-1);
		}
                struct soap *handler = soap_copy(&soap);
		g_thread_pool_push(soap_pool, handler, NULL);
	}

	LOG(LOG_DEBUG, "Signal caught, shutting down");

	soap_done(&soap);

	g_thread_pool_free(soap_pool, TRUE, TRUE);

	delete dlm;

	return 0;
}
