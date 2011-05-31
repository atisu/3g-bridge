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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Conf.h"
#include "DBHandler.h"
#include "DownloadManager.h"
#include "Job.h"
#include "QMException.h"
#include "Util.h"

#include <string>
#include <fstream>

#include <sysexits.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <sys/inotify.h>
#include <poll.h>
#include <sys/stat.h>

#include <openssl/crypto.h>
#include <openssl/md5.h>
#include <curl/curl.h>
#include <uuid/uuid.h>

#include "soap/SubmitterH.h"
#include "soap/Submitter.nsmap"

#include <glib.h>

using namespace std;

/**********************************************************************
 * Global variables
 */

/* Configuration: Name of meta-job grid */
static char const * const metaJobGrid = "Metajob"; //TODO: make configurable

/* Configuration: Where to download job input files */
static char *input_dir;

/* Configuration: Where to put output files of finished jobs */
static char *output_dir;

/* Configuration: URL prefix to substitute in place of output_dir to make valid download URLs */
static char *output_url_prefix;

/* Configuration: location of DB reread trigger file */
static char *dbreread_file;

/* If 'true', exit was requested by a signal */
static volatile bool finish;

/* If 'true', the log file should be re-opened */
static volatile bool reload;

/* The global configuration */
static GKeyFile *global_config = NULL;

/* Thread pool for serving SOAP requests */
static GThreadPool *soap_pool;

/* Command line: Location of the config file */
static char *config_file = (char *)SYSCONFDIR "/3g-bridge.conf";

/* Command line: If true, run as a daemon in the background */
static int run_as_daemon = 1;

/* Command line: If true, kill a running daemon */
static int kill_daemon;

/* Command line: Force debug mode */
static int debug_mode;

/* Command line: Print the version */
static int get_version;

/* Prefix for DIME transferred files */
static char *dime_prefix;

/* DB reread thread */
static GThread *dbreread_thread;

/* Table of the command-line options */
static GOptionEntry options[] =
{
	{ "config",	'c',	0,			G_OPTION_ARG_FILENAME,	&config_file,
		"Configuration file to use", "FILE" },
	{ "nofork",	'f',	G_OPTION_FLAG_REVERSE,	G_OPTION_ARG_NONE,	&run_as_daemon,
		"Don't detach from the terminal and run in the foreground", NULL },
	{ "debug",	'd',	0,			G_OPTION_ARG_NONE,	&debug_mode,
		"Debug mode: don't fork, log to stdout", NULL },
	{ "kill",	'k',	0,			G_OPTION_ARG_NONE,	&kill_daemon,
		"Kill the running daemon", NULL },
	{ "version",		'V',	0,	G_OPTION_ARG_NONE,		&get_version,
		"Print the version and exit", NULL },
	{ NULL }
};

/* Hack for gSoap */
struct Namespace namespaces[] = {{ NULL, }};


/* Temporary struct for DIME */
typedef struct {
    FILE *fd;
    char *path;
    MD5_CTX md5_ctx;
    unsigned char digest[16];
    char digeststr[33];
} dime_md5;


/**********************************************************************
 * Calculate file locations
 */

static string make_hashed_dir(const string &base, const string &jobid, bool create = true)
{
	string dir = base + '/' + jobid.at(0) + jobid.at(1);
	if (create)
	{
		int ret = mkdir(dir.c_str(), 0750);
		if (ret == -1 && errno != EEXIST)
			throw new QMException("Failed to create directory '%s': %s",
				dir.c_str(), strerror(errno));
	}
	dir += '/' + jobid;
	if (create)
	{
		int ret = mkdir(dir.c_str(), 0750);
		if (ret == -1 && errno != EEXIST)
			throw new QMException("Failed to create directory '%s': %s",
				dir.c_str(), strerror(errno));
	}
	return dir;
}

static string calc_input_path(const string &jobid, const string &localName)
{
	return make_hashed_dir(input_dir, jobid) + '/' + localName;
}

static string calc_output_path(const string &jobid, const string &localName)
{
	return make_hashed_dir(output_dir, jobid) + '/' + localName;
}

static string getdimeattid(const string &dimeid)
{
	string attid = dimeid.substr(0, dimeid.find("="));
	return attid;
}

static string getdimefname(const string &dimeid)
{
	size_t fe = dimeid.find_first_of("=");
	size_t le = dimeid.find_last_of("=");
	string fname = dimeid.substr(fe+1, le-fe-1);
	return fname;
}

/* dimeid is of format id=fname=url */
static string getdimepath(const string &dimeid)
{
	string fname = input_dir + string("/") + dime_prefix + "_" + getdimefname(dimeid) + "_" + getdimeattid(dimeid);
	return fname;
}


/**********************************************************************
 * Calculate the location where an output file will be stored
 */

static string calc_output_url(const string path)
{
	/* XXX Verify the prefix */
	return (string)output_url_prefix + "/" + path.substr(strlen(output_dir) + 1);
}

/**********************************************************************
 * gSOAP streaming DIME realted functions
 */
static void *fdimewriteopen(struct soap *soap, const char *id, const char *type, const char *options)
{
	dime_md5 *tdmd5 = (dime_md5 *)malloc(sizeof(dime_md5));
	if (!tdmd5)
	{
		LOG(LOG_ERR, "DIME: malloc() failed!");
		return NULL;
	}

	LOG(LOG_DEBUG, "Path: %s", getdimepath(id).c_str());
	tdmd5->path = strdup(getdimepath(id).c_str());
	if (!tdmd5->path)
	{
		LOG(LOG_ERR, "DIME: strdup() failed!");
		free(tdmd5);
		return NULL;
	}

	tdmd5->fd = fopen(tdmd5->path, "w");
	if (!tdmd5->fd)
	{
		LOG(LOG_ERR, "DIME: fopen() failed: %s", strerror(errno));
		free(tdmd5->path);
		free(tdmd5);
		return NULL;
	}

	if (!MD5_Init(&(tdmd5->md5_ctx)))
	{
		LOG(LOG_ERR, "DIME: MD5_Init() failed!");
		free(tdmd5->path);
		free(tdmd5);
	}

	return tdmd5;
}

static int fdimewrite(struct soap *soap, void *handle, const char *buf, size_t len)
{
	dime_md5 *tdmd5 = (dime_md5 *)handle;

	if (!tdmd5)
		return SOAP_FATAL_ERROR;

	fwrite(buf, 1, len, tdmd5->fd);
	if (ferror(tdmd5->fd))
	{
		LOG(LOG_ERR, "DIME: fwrite() failed!");
		fclose(tdmd5->fd);
		free(tdmd5->path);
		free(tdmd5);
		return SOAP_FATAL_ERROR;
	}

	if (!MD5_Update(&(tdmd5->md5_ctx), buf, len))
	{
		LOG(LOG_ERR, "DIME: MD5_Update() failed!");
		fclose(tdmd5->fd);
		free(tdmd5->path);
		free(tdmd5);
		return SOAP_FATAL_ERROR;
	}

	return SOAP_OK;
}

static void fdimewriteclose(struct soap *soap, void *handle)
{
	dime_md5 *tdmd5 = (dime_md5 *)handle;

	if (!tdmd5)
		return;

	fclose(tdmd5->fd);

	MD5_Final(tdmd5->digest, &(tdmd5->md5_ctx));
	for (unsigned i = 0; i < 16; i++)
		sprintf(tdmd5->digeststr+2*i, "%02x", tdmd5->digest[i]);

	string md5fname = tdmd5->path + string(".md5");
	ofstream md5file(md5fname.c_str(), ios::out | ios::app);
	if (md5file.is_open())
	{
		md5file << tdmd5->digeststr;
		md5file.close();
	}
	free(tdmd5->path);
	free(tdmd5);
}

/**********************************************************************
 * Check whether a file is a meta-job specification.
 */
bool isMetaJobSpec(const string &fname)
{
	return !strncmp("_3gb-metajob", fname.c_str(), 12); //startswith
}

/**********************************************************************
 * Check whether submitted job is meta-job.
 * Throws exception if multiple _3gb-metajob* files are specified.
 */
bool checkMetaJob(const G3BridgeSubmitter__Job &wsjob) throw(int)
{
	bool isMetaJob = false;
	for (vector<G3BridgeSubmitter__LogicalFile *>::const_iterator inpit = wsjob.inputs.begin(); inpit != wsjob.inputs.end(); inpit++)
	{
		if (isMetaJobSpec((*inpit)->logicalName))
		{
			// If multiple meta-job files specified -- reject
			if (isMetaJob)
				throw 1;
			else
				isMetaJob = true; //Don't break, check whether there are too many specified
		}
	}

	return isMetaJob;
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
	virtual ~DBItem() {};

	const string &getJobId() const { return jobId; };

	virtual void finished();
	virtual void failed();
	virtual void setRetry(const struct timeval &when, int retries);
};

DBItem::DBItem(const string &jobId, const string &logicalFile, const string &url):
		DLItem(url, calc_input_path(jobId, logicalFile)),
		jobId(jobId),
		logicalFile(logicalFile)
{
}

void DBItem::finished()
{
	try
	{
		DLItem::finished();
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "Job %s: %s", jobId.c_str(), e->what());
		delete e;
		failed();
		return;
	}

	DBHandler *dbh = DBHandler::get();
	dbh->deleteDL(jobId, logicalFile);


	/* Check if the job is now ready to be submitted */
	if (0 == dbh->getDLCount(jobId))
	{
		auto_ptr<Job> job = dbh->getJob(jobId);
		job->setStatus(Job::INIT);
		LOG(LOG_INFO, "Job %s: Preparation complete", jobId.c_str());
	}

	DBHandler::put(dbh);
}

void DBItem::failed()
{
	DLItem::failed();

	DBHandler *dbh = DBHandler::get();
	dbh->deleteDL(jobId, logicalFile);
	auto_ptr<Job> job = dbh->getJob(jobId);
	DBHandler::put(dbh);

	if (!job.get())
		return;

	if (job->getStatus() != Job::ERROR)
	{
		job->setStatus(Job::ERROR);
		LOG(LOG_NOTICE, "Job %s: Aborted due to errors", jobId.c_str());
	}

	/* Abort the download of the other input files */
	auto_ptr< vector<string> > inputs = job->getInputs();
	for (vector<string>::const_iterator it = inputs->begin(); it != inputs->end(); it++)
		DownloadManager::abort((job->getInputRef(*it)).getURL());
}

void DBItem::setRetry(const struct timeval &when, int retries)
{
	DLItem::setRetry(when, retries);

	DBHandler *dbh = DBHandler::get();
	dbh->updateDL(jobId, logicalFile, when, retries);
	DBHandler::put(dbh);
}

/**********************************************************************
 * Web service routines
 */

int __G3BridgeSubmitter__submit(struct soap *soap, G3BridgeSubmitter__JobList *jobs, G3BridgeSubmitter__JobIDList *result)
{
	DBHandler *dbh;
	LOG(LOG_INFO, "Starting submission...");

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

	for (vector<G3BridgeSubmitter__Job *>::const_iterator jobit = jobs->job.begin(); jobit != jobs->job.end(); jobit++)
	{
		uuid_t uuid;
		char jobid[37];

		uuid_generate(uuid);
		uuid_unparse(uuid, jobid);

		vector< pair<string, FileRef> > inputs;

		G3BridgeSubmitter__Job *wsjob = *jobit;

		LOG(LOG_INFO, "Checking if meta-job");
		bool isMetaJob = false;
		try { isMetaJob = checkMetaJob(*wsjob); }
		catch (int)
		{
			LOG(LOG_ERR,
			    "submit: Multiple _3gb-metajob "
			    "files are specified.");
			return SOAP_FATAL_ERROR; //TODO: check
		}

		if (isMetaJob)
			LOG(LOG_INFO, "Yes, this is a meta-job");

		char const * destinationGrid =
			isMetaJob
			? metaJobGrid
			: wsjob->grid.c_str();
		LOG(LOG_DEBUG, "Destination grid: '%s'", destinationGrid);
		Job *qmjob = new Job((const char *)jobid, wsjob->alg.c_str(), destinationGrid, wsjob->args.c_str(), Job::INIT, &wsjob->env);
		
		LOG(LOG_DEBUG, "Setting tag");
		if (wsjob->tag)
			qmjob->setTag(*(wsjob->tag));

		LOG(LOG_DEBUG, "Adding inputs");
		for (vector<G3BridgeSubmitter__LogicalFile *>::const_iterator inpit = wsjob->inputs.begin(); inpit != wsjob->inputs.end(); inpit++)
		{
			G3BridgeSubmitter__LogicalFile *lfn = *inpit;

			string URL = lfn->URL;
			string *md5 = lfn->md5;
			string *size = lfn->size;
			string fname = lfn->logicalName;
		      			
			if ('/' != URL[0])
			{
				FileRef a(URL, md5 ? md5->c_str() : NULL, size ? atoi(size->c_str()) : -1);
				qmjob->addInput(fname, a);
				inputs.push_back(pair<string, FileRef>(fname, a));
			}
			else
			{
				string md5, dimeid;
				struct soap_multipart *attachment;
				for (attachment = soap->dime.list; attachment; attachment = attachment->next)
				{
					dimeid = attachment->id;
					if (getdimefname(dimeid) == fname)
						break;
				}
				string path = calc_input_path(jobid, getdimefname(dimeid));
				string dimepath = getdimepath(dimeid);
				string md5path = dimepath + ".md5";
				if (-1 == link(dimepath.c_str(), path.c_str()))
				{
					LOG(LOG_ERR, "DC-API-Single: failed to copy input file %s",
						dimepath.c_str());
					delete qmjob;
					return SOAP_FATAL_ERROR;
				}
				ifstream md5file(md5path.c_str());
				if (!md5file.is_open())
				{
					LOG(LOG_ERR, "DC-API-Single: failed to open MD5 hash file  %s",
						md5path.c_str());
					delete qmjob;
					return SOAP_FATAL_ERROR;
				}
				md5file >> md5;
				md5file.close();
				struct stat st;
				stat(dimepath.c_str(), &st);
				FileRef a(path, md5.c_str(), st.st_size);
				qmjob->addInput(fname, a);
				inputs.push_back(pair<string, FileRef>(fname, a));
				unlink(md5path.c_str());
				unlink(dimepath.c_str());
			}
		}

		LOG(LOG_DEBUG, "Adding outputs");
		for (vector<string>::const_iterator outit = wsjob->outputs.begin(); outit != wsjob->outputs.end(); outit++)
		{
			string path = calc_output_path(jobid, *outit);
			qmjob->addOutput(*outit, path);
		}

		LOG(LOG_DEBUG, "Saving job");
		if (!dbh->addJob(*qmjob))
		{
			LOG(LOG_ERR, "Failed to add job, removing any other job added so far within this request.");
			for (vector<string>::const_iterator idit = result->jobid.begin(); idit != result->jobid.end(); idit++)
				dbh->deleteJob(*idit);
			return SOAP_FATAL_ERROR;
		}

		if (isMetaJob)
		{
			//Store original destination grid
			LOG(LOG_INFO, "Meta-job original grid: '%s'", wsjob->grid.c_str());
			qmjob->setGridId(wsjob->grid);
		}

		if (isMetaJob)
			LOG(LOG_INFO, "Job %s: Accepted as meta-job", jobid);
		else
			LOG(LOG_INFO, "Job %s: Accepted", jobid);

		logit_mon("event=job_entry job_id=%s application=%s", jobid, qmjob->getName().c_str());

		result->jobid.push_back(jobid);

		delete qmjob;
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}

int __G3BridgeSubmitter__getStatus(struct soap *soap, G3BridgeSubmitter__JobIDList *jobids, G3BridgeSubmitter__StatusList *result)
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
		G3BridgeSubmitter__JobStatus status = G3BridgeSubmitter__JobStatus__UNKNOWN;

		auto_ptr<Job> job = dbh->getJob(*it);
		if (job.get()) switch (job->getStatus())
		{
			case Job::PREPARE:
			case Job::INIT:
				status = G3BridgeSubmitter__JobStatus__INIT;
				break;
			case Job::RUNNING:
				status = G3BridgeSubmitter__JobStatus__RUNNING;
				break;
			case Job::FINISHED:
				status = G3BridgeSubmitter__JobStatus__FINISHED;
				break;
			case Job::TEMPFAILED:
				status = G3BridgeSubmitter__JobStatus__TEMPFAILED;
				break;
			case Job::ERROR:
			case Job::CANCEL:
				status = G3BridgeSubmitter__JobStatus__ERROR;
				break;
		}

		result->status.push_back(status);
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}


int __G3BridgeSubmitter__delete(struct soap *soap, G3BridgeSubmitter__JobIDList *jobids, struct __G3BridgeSubmitter__deleteResponse &result G_GNUC_UNUSED)
{
	//TODO: delete files

	DBHandler *dbh;

	try
	{
		dbh = DBHandler::get();
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "delete: Failed to get a DB handle: %s", e->what());
		delete(e);
		return SOAP_FATAL_ERROR;
	}
	catch (...)
	{
		LOG(LOG_ERR, "delete: Failed to get a DB handle: Unknown exception");
		return SOAP_FATAL_ERROR;
	}

	for (vector<string>::const_iterator it = jobids->jobid.begin(); it != jobids->jobid.end(); it++)
	{
		auto_ptr<Job> job = dbh->getJob(*it);
		if (!job.get())
		{
			LOG(LOG_NOTICE, "delete: Unknown job ID %s", it->c_str());
			continue;
		}

		/* Delete the input/output files */
		auto_ptr< vector<string> > files = job->getInputs();
		for (vector<string>::const_iterator fsit = files->begin(); fsit != files->end(); fsit++)
		{
			string path = (job->getInputRef(*fsit)).getURL();
			DownloadManager::abort(path);
			unlink(path.c_str());
		}
		files = job->getOutputs();
		for (vector<string>::const_iterator fsit = files->begin(); fsit != files->end(); fsit++)
		{
			string path = job->getOutputPath(*fsit);
			unlink(path.c_str());
		}

		/* Delete the input/output directories */
		string jobdir = make_hashed_dir(input_dir, *it, false);
		rmdir(jobdir.c_str());
		jobdir = make_hashed_dir(output_dir, *it, false);
		rmdir(jobdir.c_str());

		if (job->getStatus() == Job::RUNNING)
		{
			job->setStatus(Job::CANCEL);
			LOG(LOG_NOTICE, "Job %s: Cancelled", it->c_str());
		}
		else
		{
			dbh->deleteJob(*it);
			LOG(LOG_NOTICE, "Job %s: Deleted", it->c_str());
		}
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}


int __G3BridgeSubmitter__getOutput(struct soap *soap, G3BridgeSubmitter__JobIDList *jobids, G3BridgeSubmitter__OutputList *result)
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

		G3BridgeSubmitter__JobOutput *jout = soap_new_G3BridgeSubmitter__JobOutput(soap, -1);
		result->output.push_back(jout);

		auto_ptr<Job> job = dbh->getJob(*it);
		if (!job.get())
		{
			LOG(LOG_NOTICE, "getOutput: Unknown job ID %s", it->c_str());
			continue;
		}

		auto_ptr< vector<string> > files = job->getOutputs();
		for (vector<string>::const_iterator fsit = files->begin(); fsit != files->end(); fsit++)
		{
			string path = job->getOutputPath(*fsit);

			G3BridgeSubmitter__LogicalFile *lf = soap_new_G3BridgeSubmitter__LogicalFile(soap, -1);
			lf->logicalName = *fsit;
			lf->URL = calc_output_url(path);
			jout->output.push_back(lf);
		}
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}


int __G3BridgeSubmitter__getFinished(struct soap *soap, string grid, G3BridgeSubmitter__JobIDList *result)
{
	DBHandler *dbh;

	try
	{
		dbh = DBHandler::get();
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "getFinished: Failed to get a DB handle: %s", e->what());
		delete(e);
		return SOAP_FATAL_ERROR;
	}
	catch (...)
	{
		LOG(LOG_ERR, "getFinished: Failed to get a DB handle: Unknown exception");
		return SOAP_FATAL_ERROR;
	}

	JobVector jobs;
	dbh->getFinishedJobs(jobs, grid, 500);

	for (JobVector::const_iterator it = jobs.begin(); it != jobs.end(); it++)
		result->jobid.push_back((*it)->getId());

	DBHandler::put(dbh);
	return SOAP_OK;
}


int __G3BridgeSubmitter__getVersion(struct soap *soap, std::string &resp)
{
	resp = PACKAGE_STRING;
	return SOAP_OK;
}


int __G3BridgeSubmitter__getGridData(struct soap *soap, G3BridgeSubmitter__JobIDList *jobids, G3BridgeSubmitter__GridDataList *result)
{
	DBHandler *dbh;

	try
	{
		dbh = DBHandler::get();
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "getGridData: Failed to get a DB handle: %s", e->what());
		delete(e);
		return SOAP_FATAL_ERROR;
	}
	catch (...)
	{
		LOG(LOG_ERR, "getGridData: Failed to get a DB handle: Unknown exception");
		return SOAP_FATAL_ERROR;
	}

	for (vector<string>::const_iterator it = jobids->jobid.begin(); it != jobids->jobid.end(); it++)
	{
		string griddata = "";

		auto_ptr<Job> job = dbh->getJob(*it);
		if (!job.get())
			griddata = "UNKNOWN JOB";
		else if ("" != job->getGridData())
			griddata = job->getGridData();
		result->griddata.push_back(griddata);
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
		Submitter_serve(soap);
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
 * Misc. helper functions
 */

static void sigint_handler(int signal __attribute__((__unused__)))
{
	finish = true;
}

static void sighup_handler(int signal __attribute__((__unused__)))
{
	reload = true;
}

static void restart_download(const char *jobid, const char *localName,
	const char *url, const struct timeval *next, int retries)
{
	DBItem *item = new DBItem(jobid, localName, url);
	item->setRetry(*next, retries);
	DBHandler *dbh = DBHandler::get();
	dbh->updateInputPath(jobid, localName, calc_input_path(jobid, localName));
	DBHandler::put(dbh);
	DownloadManager::add(item);
}

static void *run_dbreread(void *data G_GNUC_UNUSED)
{
	int fd;

	if (touch(dbreread_file))
	{
		LOG(LOG_ERR, "Failed to create DB reread trigger file '%s': %s",
			dbreread_file, strerror(errno));
		return NULL;
	}

	fd = inotify_init();
	if (-1 == fd)
	{
		LOG(LOG_ERR, "Failed to initialize inotify: %s",
			strerror(errno));
		return NULL;
	}

	/* Watch only the open event */
	if (-1 ==inotify_add_watch(fd, dbreread_file, IN_OPEN))
	{
		LOG(LOG_ERR, "Failed to initialize inotify: %s",
			strerror(errno));
		close(fd);
		return NULL;
	}

	while (!finish)
	{
		int rv;
		struct pollfd pfd;

		pfd.fd = fd;
		pfd.events = POLLIN;

		rv = poll(&pfd, 1, 5);
		if (-1 == rv)
			LOG(LOG_ERR, "poll() failed: %s", strerror(errno));
		else
		{
			struct inotify_event ev;
			read(fd, &ev, sizeof(ev));
			DBHandler *dbh = DBHandler::get();
			dbh->getAllDLs(restart_download);
			DBHandler::put(dbh);
		}
	}
	return NULL;
}


/**********************************************************************
 * The main program
 */

int main(int argc, char **argv)
{
	GOptionContext *context;
	int port, ws_threads;
	GError *error = NULL;
	struct sigaction sa;
	struct soap soap;

	uuid_t uuid;
	dime_prefix = new char[37];
	uuid_generate(uuid);
	uuid_unparse(uuid, dime_prefix);

	/* Parse the command line */
	context = g_option_context_new("- Web Service interface to the 3G Bridge");
	g_option_context_add_main_entries(context, options, PACKAGE);
        if (!g_option_context_parse(context, &argc, &argv, &error))
	{
		LOG(LOG_ERR, "Failed to parse the command line options: %s", error->message);
		exit(EX_USAGE);
	}
	g_option_context_free(context);

	if (get_version)
	{
		cout << PACKAGE_STRING << endl;
		exit(EX_OK);
	}

	if (!config_file)
	{
		LOG(LOG_ERR, "The configuration file is not specified");
		exit(EX_USAGE);
	}

	global_config = g_key_file_new();
	g_key_file_load_from_file(global_config, config_file, G_KEY_FILE_NONE, &error);
	if (error)
	{
		LOG(LOG_ERR, "Failed to load the config file: %s", error->message);
		g_error_free(error);
		exit(EX_NOINPUT);
	}

	if (kill_daemon)
		exit(pid_file_kill(global_config, GROUP_WSSUBMITTER));

	if (debug_mode)
	{
		log_init_debug();
		run_as_daemon = 0;
	}
	else
		log_init(global_config, GROUP_WSSUBMITTER);

	port = g_key_file_get_integer(global_config, GROUP_WSSUBMITTER, "port", &error);
	if (!port || error)
	{
		LOG(LOG_ERR, "Failed to retrieve the listener port: %s", error->message);
		g_error_free(error);
		exit(EX_DATAERR);
	}
	if (port <= 0 || port > 65535)
	{
		LOG(LOG_ERR, "Invalid port number (%d) specified", port);
		exit(EX_DATAERR);
	}

	ws_threads = g_key_file_get_integer(global_config, GROUP_WSSUBMITTER, "service-threads", &error);
	if (!ws_threads || error)
	{
		LOG(LOG_ERR, "Failed to parse the number of service threads: %s", error->message);
		g_error_free(error);
		exit(EX_DATAERR);
	}
	if (ws_threads <= 0 || ws_threads > 1000)
	{
		LOG(LOG_ERR, "Invalid thread number (%d) specified", ws_threads);
		exit(EX_DATAERR);
	}

	input_dir = g_key_file_get_string(global_config, GROUP_WSSUBMITTER, "input-dir", &error);
	if (!input_dir || error)
	{
		LOG(LOG_ERR, "Failed to get the input directory: %s", error->message);
		g_error_free(error);
		exit(EX_DATAERR);
	}
	g_strstrip(input_dir);

	output_dir = g_key_file_get_string(global_config, GROUP_WSSUBMITTER, "output-dir", &error);
	if (!output_dir || error)
	{
		LOG(LOG_ERR, "Failed to get the output base directory: %s", error->message);
		g_error_free(error);
		exit(EX_DATAERR);
	}
	g_strstrip(output_dir);

	output_url_prefix = g_key_file_get_string(global_config, GROUP_WSSUBMITTER, "output-url-prefix", &error);
	if (!output_url_prefix || error)
	{
		LOG(LOG_ERR, "Failed to get the output URL prefix: %s", error->message);
		g_error_free(error);
		exit(EX_DATAERR);
	}
	g_strstrip(output_url_prefix);

	dbreread_file = g_key_file_get_string(global_config, GROUP_WSSUBMITTER, "dbreread-file", &error);
	if (error)
	{
		LOG(LOG_ERR, "Failed to get DB reread file's location: %s",
			error->message);
		g_error_free(error);
		dbreread_file = NULL;
	}

	if (run_as_daemon && pid_file_create(global_config, GROUP_WSSUBMITTER))
		exit(EX_OSERR);

	/* Set up the signal handlers */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	/* BOINC sends SIGHUP when it wants to kill the process */
	if (run_as_daemon)
		sa.sa_handler = sighup_handler;
	sigaction(SIGHUP, &sa, NULL);

	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);

	/* Initialize glib's thread system */
	g_thread_init(NULL);

	if (run_as_daemon)
	{
		daemon(0, 0);
		pid_file_update();
	}

	try
	{
		DBHandler::init(global_config);
		DownloadManager::init(global_config, GROUP_WSSUBMITTER);

		DBHandler *dbh = DBHandler::get();
		dbh->getAllDLs(restart_download);
		DBHandler::put(dbh);
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "Fatal: %s", e->what());
		delete e;
		exit(EX_SOFTWARE);
	}

	/* Launch db reread thread */
	if (dbreread_file)
	{
		dbreread_thread = g_thread_create(run_dbreread, NULL, TRUE, &error);
		if (!dbreread_thread)
		{
			LOG(LOG_ERR, "Failed to create DB reread thread: %s",
				error->message);
			g_error_free(error);
		}
	}

	soap_init2(&soap, SOAP_IO_KEEPALIVE, SOAP_IO_KEEPALIVE | SOAP_IO_CHUNK);
	soap_set_namespaces(&soap, Submitter_namespaces);
	soap.send_timeout = 60;
	soap.recv_timeout = 60;
	/* Give a small accept timeout to detect exit signals quickly */
	soap.accept_timeout = 1;
	soap.max_keep_alive = 100;
	soap.socket_flags = MSG_NOSIGNAL;
	soap.bind_flags = SO_REUSEADDR;
	soap.fdimewriteopen = fdimewriteopen;
	soap.fdimewrite = fdimewrite;
	soap.fdimewriteclose = fdimewriteclose;

	SOAP_SOCKET ss = soap_bind(&soap, NULL, port, 100);
	if (!soap_valid_socket(ss))
	{
		char buf[256];
		soap_sprint_fault(&soap, buf, sizeof(buf));
		LOG(LOG_ERR, "SOAP initialization: %s", buf);
		exit(EX_UNAVAILABLE);
	}

	soap_pool = g_thread_pool_new(soap_service_handler, NULL, ws_threads, TRUE, NULL);
	if (!soap_pool)
	{
		LOG(LOG_ERR, "Failed to launch the WS threads");
		exit(EX_UNAVAILABLE);
	}

	while (!finish)
	{
		if (reload)
		{
			log_reopen();
			reload = false;
		}

		SOAP_SOCKET ret = soap_accept(&soap);
		if (ret == SOAP_INVALID_SOCKET)
		{
			if (!soap.errnum)
				continue;
			soap_print_fault(&soap, stderr);
			/* XXX Should we really exit here? */
			exit(EX_UNAVAILABLE);
		}
		struct soap *handler = soap_copy(&soap);
		g_thread_pool_push(soap_pool, handler, NULL);
	}

	LOG(LOG_DEBUG, "Signal caught, shutting down");

	soap_destroy(&soap);
	soap_end(&soap);
	soap_done(&soap);

	g_thread_pool_free(soap_pool, TRUE, TRUE);

	DownloadManager::done();
	DBHandler::done();

	g_key_file_free(global_config);
	g_free(input_dir);
	g_free(output_dir);
	g_free(output_url_prefix);

	return EX_OK;
}
