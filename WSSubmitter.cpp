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
#include <errno.h>

#include <openssl/crypto.h>
#include <curl/curl.h>
#include <uuid/uuid.h>

#include "soap/soapH.h"
#include "soap/G3BridgeSubmitter.nsmap"

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

static string calc_input_path(const string jobid, const string localName) throw (QMException *)
{
	string jobdir = (string)download_dir + "/" + jobid;
	int ret = mkdir(jobdir.c_str(), 0750);
	if (ret == -1 && errno != EEXIST)
		throw new QMException("Failed to create directory '%s': %s",
			jobdir.c_str(), strerror(errno));

	return jobdir + "/" + localName;
}

/**********************************************************************
 * Calculate the file system location where an input file should be downloaded to
 */

static string calc_temp_path(const string jobid, const string localName)
{
	return (string)partial_dir + "/" + jobid + "_" + localName;
}

/**********************************************************************
 * Calculate the location where an output file will be stored
 */

static string calc_output_path(const string jobid, const string localName) throw (QMException *)
{
	string jobdir = (string)output_dir + "/" + jobid;
	int ret = mkdir(jobdir.c_str(), 0750);
	if (ret == -1 && errno != EEXIST)
		throw new QMException("Failed to create directory '%s': %s",
			jobdir.c_str(), strerror(errno));

	return jobdir + "/" + localName;
}

/**********************************************************************
 * Calculate the location where an output file will be stored
 */

static string calc_output_url(const string path)
{
	/* XXX Verify the prefix */
	return (string)output_url_prefix + "/" + path.substr(strlen(output_dir));
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

	const string &getJobId() const { return jobId; };

	void finished();
	void failed();
};

DBItem::DBItem(const string &jobId, const string &logicalFile, const string &URL)
{
	DLItem::DLItem();
	this->url = URL;
	this->path = calc_temp_path(jobId, logicalFile);
}

void DBItem::finished()
{
	DLItem::finished();

	DBHandler *dbh = DBHandler::get();
	dbh->deleteDL(jobId, logicalFile);
	auto_ptr<Job> job = dbh->getJob(jobId);
	DBHandler::put(dbh);

	/* If the job has already failed, just do not bother */
	if (!job.get() || job->getStatus() != Job::PREPARE)
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
	DLItem::failed();

	DBHandler *dbh = DBHandler::get();
	dbh->deleteDL(jobId, logicalFile);
	auto_ptr<Job> job = dbh->getJob(jobId);
	dbh->updateJobStat(jobId, Job::ERROR);
	DBHandler::put(dbh);

	if (!job.get())
		return;

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

int __G3Bridge__submit(struct soap *soap, G3Bridge__JobList *jobs, struct G3Bridge__JobIDList *result)
{
	DBHandler *dbh;

	try
	{
		dbh = DBHandler::get();
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "submit: Failed to get a DB handle: %s", e->what());
		delete(e);
		return SOAP_FATAL_ERROR;
	}
	catch (...)
	{
		LOG(LOG_ERR, "submit: Failed to get a DB handle: Unknown exception");
		return SOAP_FATAL_ERROR;
	}

	for (vector<G3Bridge__Job *>::const_iterator jobit = jobs->job.begin(); jobit != jobs->job.end(); jobit++)
	{
		uuid_t uuid;
		char jobid[37];

		uuid_generate(uuid);
		uuid_unparse(uuid, jobid);

		vector< pair<string, string> > inputs;

		G3Bridge__Job *wsjob = *jobit;
		Job qmjob((const char *)jobid, wsjob->alg.c_str(), wsjob->grid.c_str(), wsjob->args.c_str(), Job::PREPARE);

		for (vector<G3Bridge__LogicalFile *>::const_iterator inpit = wsjob->inputs.begin(); inpit != wsjob->inputs.end(); inpit++)
		{
			G3Bridge__LogicalFile *lfn = *inpit;

			string path = calc_input_path(jobid, lfn->logicalName);
			qmjob.addInput(lfn->logicalName, path);

			inputs.push_back(pair<string, string>(lfn->logicalName, lfn->URL));
		}

		for (vector<string>::const_iterator outit = wsjob->outputs.begin(); outit != wsjob->outputs.end(); outit++)
		{
			string path = calc_output_path(jobid, *outit);
			qmjob.addOutput(*outit, path);
		}

		dbh->addJob(qmjob);

		/* The downloads can only be started after the job record is in the DB */
		for (vector< pair<string,string> >::const_iterator it = inputs.begin(); it != inputs.end(); it++)
		{
			auto_ptr<DBItem> item(new DBItem(qmjob.getId(), it->first, it->second));
			dbh->addDL(qmjob.getId(), it->first, it->second);
			dlm->add(item.get());
			item.release();
		}
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}

int __G3Bridge__getStatus(struct soap *soap, G3Bridge__JobIDList *jobids, struct G3Bridge__StatusList *result)
{
	DBHandler *dbh;

	try
	{
		dbh = DBHandler::get();
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "getStatus: Failed to get a DB handle: %s", e->what());
		delete(e);
		return SOAP_FATAL_ERROR;
	}
	catch (...)
	{
		LOG(LOG_ERR, "getStatus: Failed to get a DB handle: Unknown exception");
		return SOAP_FATAL_ERROR;
	}

	for (vector<string>::const_iterator it = jobids->jobid.begin(); it != jobids->jobid.end(); it++)
	{
		G3Bridge__JobStatus status = G3Bridge__JobStatus__UNKNOWN;

		auto_ptr<Job> job;

		job = dbh->getJob(*it);

		if (job.get()) switch (job->getStatus())
		{
			case Job::PREPARE:
			case Job::INIT:
				status = G3Bridge__JobStatus__INIT;
				break;
			case Job::RUNNING:
				status = G3Bridge__JobStatus__RUNNING;
				break;
			case Job::FINISHED:
				status = G3Bridge__JobStatus__FINISHED;
				break;
			case Job::ERROR:
			case Job::CANCEL:
				status = G3Bridge__JobStatus__ERROR;
				break;
		}

		result->status.push_back(status);
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}


int __G3Bridge__delJob(struct soap *soap, G3Bridge__JobIDList *jobids, struct __G3Bridge__delJobResponse &result G_GNUC_UNUSED)
{
	DBHandler *dbh;

	try
	{
		dbh = DBHandler::get();
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "delJob: Failed to get a DB handle: %s", e->what());
		delete(e);
		return SOAP_FATAL_ERROR;
	}
	catch (...)
	{
		LOG(LOG_ERR, "delJob: Failed to get a DB handle: Unknown exception");
		return SOAP_FATAL_ERROR;
	}

	for (vector<string>::const_iterator it = jobids->jobid.begin(); it != jobids->jobid.end(); it++)
	{
		auto_ptr<Job> job = dbh->getJob(*it);
		if (!job.get())
			continue;

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

		string jobdir = (string)download_dir + "/" + *it;
		rmdir(jobdir.c_str());

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

		jobdir = (string)output_dir + "/" + *it;
		rmdir(jobdir.c_str());

		/* Delete the job itself */
		dbh->deleteJob(*it);
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}


int __G3Bridge__getOutput(struct soap *soap, G3Bridge__JobIDList *jobids, struct G3Bridge__OutputList *result)
{
	DBHandler *dbh;

	try
	{
		dbh = DBHandler::get();
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "getOutput: Failed to get a DB handle: %s", e->what());
		delete(e);
		return SOAP_FATAL_ERROR;
	}
	catch (...)
	{
		LOG(LOG_ERR, "getOutput: Failed to get a DB handle: Unknown exception");
		return SOAP_FATAL_ERROR;
	}

	for (vector<string>::const_iterator it = jobids->jobid.begin(); it != jobids->jobid.end(); it++)
	{

		G3Bridge__JobOutput *jout = soap_new_G3Bridge__JobOutput(soap, -1);
		jout->jobid = *it;
		result->output.push_back(jout);

		auto_ptr<Job> job = dbh->getJob(*it);
		if (!job.get())
			continue;

		auto_ptr< vector<string> > files = job->getOutputs();
		for (vector<string>::const_iterator fsit = files->begin(); fsit != files->end(); fsit++)
		{
			string path = job->getOutputPath(*fsit);

			G3Bridge__LogicalFile *lf = soap_new_G3Bridge__LogicalFile(soap, -1);
			lf->logicalName = *fsit;
			lf->URL = calc_output_url(path);
			jout->output.push_back(lf);
		}
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}

/**********************************************************************
 * The SOAP thread handler
 */

static void soap_service_handler(void *data, void *user_data G_GNUC_UNUSED)
{
	struct soap *soap = (struct soap *)data;

	LOG(LOG_DEBUG, "Serving SOAP request");

	try
	{
		soap_serve(soap);
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "SOAP: Caught exception: %s", e->what());
		delete e;
	}
	catch (...)
	{
		LOG(LOG_ERR, "SOAP: Caught unhandled exception");
	}

	soap_destroy(soap);
	soap_end(soap);
	soap_free(soap);

	LOG(LOG_DEBUG, "Finished serving SOAP request");
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
	soap.bind_flags = SO_REUSEADDR;

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

	soap_destroy(&soap);
	soap_end(&soap);
	soap_done(&soap);

	g_thread_pool_free(soap_pool, TRUE, TRUE);
	delete dlm;
	g_key_file_free(global_config);
	g_free(download_dir);
	g_free(partial_dir);
	g_free(output_dir);
	g_free(output_url_prefix);

	return 0;
}
