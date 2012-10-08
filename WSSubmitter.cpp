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

/**
 * \anchor WSSubmitter
 * @file WSSubmitter.cpp
 * @brief Web Service Submitter.
 * WSSubmitter is responsible for implementing the Web Service interface of 3G
 * Bridge, and offers the DownloadManager component for downloading jobs' input
 * files.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Conf.h"
#include "DBHandler.h"
#include "Job.h"
#include "QMException.h"
#include "Util.h"

#include <string>
#include <fstream>

#include <sysexits.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>

#include <openssl/md5.h>
#include <uuid/uuid.h>

#include "soap/SubmitterH.h"
#include "soap/Submitter.nsmap"

#include <glib.h>

#ifdef HAVE_SYS_INOTIFY_H
#include <sys/inotify.h>
#elif defined(HAVE_INOTIFYTOOLS)
#include <inotifytools/inotify.h>
#endif

using namespace std;
using namespace config;

/**********************************************************************
 * Global variables
 */

/* Configuration: Name of meta-job grid */
static CSTR_C metaJobGrid = "Metajob"; //TODO: make configurable

/**
 * Finish flag.
 * The contents of this variable is 'true' if exit was requested by a signal.
 */
static volatile bool finish;

/**
 * Log file reopen flag.
 * The contents of this variable is 'true' if log file has to be reopened.
 */
static volatile bool reload;

/**
 * Configuration file object.
 */
static GKeyFile *global_config = NULL;

/**
 * Thread pool for serving SOAP requests
 */
static GThreadPool *soap_pool;

/**
 * Location of the config file.
 */
static char *config_file = (char *)SYSCONFDIR "/3g-bridge.conf";

/**
 * Flag to indicate if WSSubmitteer should be run as a daemon in the background.
 */
static int run_as_daemon = 1;

/**
 * Flag to indicate if a running daemon should be killed
 */
static int kill_daemon;

/**
 * Command-line flag to indicate debug mode.
 */
static int debug_mode;

/**
 * Command-line flag ro indicate if version should be printed to not.
 */
static int get_version;

/**
 * Prefix for DIME transferred files.
 * This variable stores the temporary location of files transferred using DIME.
 */
static char *dime_prefix;

/**
 * Table of the command-line options
 */
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

/**
 * Hack for gSOAP
 */
struct Namespace namespaces[] = {{ NULL, }};


/**
 * @struct dime_md5
 * Temporary struct for DIME transfers.
 * This structure is used to store temporary information during DIME transfers.
 */
typedef struct {
	/// pointer to FILE the structure belongs to
	FILE *fd;
	/// path of the destination file
	char *path;
	/// MD5 contect for calculating incoming file's MD5 hash
	MD5_CTX md5_ctx;
	/// MD5 digest
	unsigned char digest[16];
	/// string representation of MD5 digest
	char digeststr[33];
} dime_md5;


/**********************************************************************
 * Calculate file locations
 */

/**
 * Get a DIME attachment's identifier.
 * This function can be used to get the identifier of a DIME attachment. dimeid
 * is the DIME identifier in the form of \<id\>=\<fname\>=\<url\>.
 * @param dimeid the DIME identifier string
 * @return first part of dimeid
 */
static string getdimeattid(const string &dimeid)
{
	string attid = dimeid.substr(0, dimeid.find("="));
	return attid;
}


/**
 * Get a DIME attachment's file name.
 * This function can be used to get the file name of a DIME attachment. dimeid
 * is the DIME identifier in the form of \<id\>=\<fname\>=\<url\>.
 * @param dimeid the DIME identifier string
 * @return second part of dimeid
 */
static string getdimefname(const string &dimeid)
{
	size_t fe = dimeid.find_first_of("=");
	size_t le = dimeid.find_last_of("=");
	string fname = dimeid.substr(fe+1, le-fe-1);
	return fname;
}


/**
 * Calculate a DIME attachment's output path.
 * This function calculates the storage path of a DIME attachment on the
 * filesystem.
 * @param dimeid the DIME identifier string of form \<id\>=\<fname\>=\<url\>
 * @return path of the DIME attachment on the filesystem
 */
static string getdimepath(const string &dimeid)
{
	string fname = input_dir + string("/") + dime_prefix + "_" + getdimefname(dimeid) + "_" + getdimeattid(dimeid);
	return fname;
}

/**********************************************************************
 * gSOAP streaming DIME realted functions
 */
/**
 * Open a DIME attachment for writing.
 * This function is called by gSOAP to initialize a DIME attachment write. The
 * function allocates and initializes a dime_md5 structure for the DIME
 * attachment's transfer, and opens the destination file.
 * @see dime_md5
 * @param soap pointer to the SOAP structure
 * @param id the DIME attachment's identifier
 * @param type type of the DIME attachment
 * @param options DIME attachment options
 * @return initialized dime_md5 structure
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


/**
 * Write DIME attachment data.
 * This function is called by gSOAP to handle incoming data of a DIME
 * attachment. Practically, the incoming data is written to the DIME
 * attachment's target file, and the MD5 context is also updated.
 * @see dime_md5
 * @param soap pointer to the SOAP structure
 * @param handle pointer to the dime_md5 structure of the DIME attachment
 * @param buf the buffer with the data
 * @param len the size of the buffer
 * @return SOAP_OK if everything went OK, error otherwise
 */
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


/**
 * Terminate a DIME attachment.
 * This function is called by gSOAP to handle the end of transfer of a DIME
 * attachment. The function closes the destination file, and writes the
 * calculated MD5 hash into the a new file using the filename of the destination
 * file and a '.md5' postfix.
 * @see dime_md5
 * @param soap pointer to the SOAP structure
 * @param handle pointer to the dime_md5 structure of the DIME attachment
 */
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
 * Web service routines
 */

/**
 * Job submission handler.
 * This function is invoked if jobs are submitted through the web service interface.
 * @param soap pointer to the SOAP structure
 * @param jobs list of to submit
 * @param[out] result result(s) of job submission(s)
 * @return SOAP_OK if operation was successfull, error otherwise
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

	/// Iterates through each job.
	for (vector<G3BridgeSubmitter__Job *>::const_iterator jobit = jobs->job.begin(); jobit != jobs->job.end(); jobit++)
	{
		uuid_t uuid;
		char jobid[37];

		/// Generates a new UUID for the job.
		uuid_generate(uuid);
		uuid_unparse(uuid, jobid);

		vector< pair<string, FileRef> > inputs;

		/// Creates a new Job object for the job.
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
		/// If tag is set, sets the tag for the Job object.
		if (wsjob->tag)
			qmjob->setTag(*(wsjob->tag));

		LOG(LOG_DEBUG, "Adding inputs");
		/// Iterates through the input files.
		for (vector<G3BridgeSubmitter__LogicalFile *>::const_iterator inpit = wsjob->inputs.begin(); inpit != wsjob->inputs.end(); inpit++)
		{
			G3BridgeSubmitter__LogicalFile *lfn = *inpit;

			string URL = lfn->URL;
			string *md5 = lfn->md5;
			string *size = lfn->size;
			string fname = lfn->logicalName;
		      			
			if (fname == "" || URL == "")
			{
				qmjob->deleteJob();
				return soap_receiver_fault(soap, "Invalid input file description", "At least URL and logical name attributes have to specified for input files");
			}

			/// Either simply adds the remote file, or searches for the DIME transferred local file.
			if ('/' != URL[0])
			{
				FileRef a(URL, (md5 ? *md5 : ""), (size ? atoi(size->c_str()) : -1));
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
					LOG(LOG_ERR, "Failed to copy input file %s", dimepath.c_str());
					delete qmjob;
					return SOAP_FATAL_ERROR;
				}
				ifstream md5file(md5path.c_str());
				if (!md5file.is_open())
				{
					LOG(LOG_ERR, "Failed to open MD5 hash file  %s", md5path.c_str());
					delete qmjob;
					return SOAP_FATAL_ERROR;
				}
				md5file >> md5;
				md5file.close();
				struct stat st;
				stat(dimepath.c_str(), &st);
				FileRef a(path, md5, st.st_size);
				qmjob->addInput(fname, a);
				inputs.push_back(pair<string, FileRef>(fname, a));
				unlink(md5path.c_str());
				unlink(dimepath.c_str());
			}
		}


		LOG(LOG_DEBUG, "Adding outputs");
		/// Iterates through the output files, and adds them to the job.
		for (vector<string>::const_iterator outit = wsjob->outputs.begin(); outit != wsjob->outputs.end(); outit++)
		{
			if ("" == *outit)
			{
				qmjob->deleteJob();
				return soap_receiver_fault(soap, "Empty output file name", "Empty output file names are not allowed");
			}
			string path = calc_output_path(jobid, *outit);
			qmjob->addOutput(*outit, path);
		}

		LOG(LOG_DEBUG, "Saving job");
		/// Adds the job to the database.
		if (!dbh->addJob(*qmjob))
		{
			LOG(LOG_ERR, "Failed to add job, removing any other job added so far within this request.");
			for (vector<string>::const_iterator idit = result->jobid.begin(); idit != result->jobid.end(); idit++)
				dbh->deleteJob(*idit);
			return soap_receiver_fault(soap, "Failed to add job to database", "Failed to add job to the database. This is probably caused by using a wrong grid or algorithm name.");
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

		/// Adds the job's unique identifier to the result.
		result->jobid.push_back(jobid);

		/// Frees up memory.
		delete qmjob;
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}


/**
 * Get jobs' status.
 * This function is invoked if a client is about the query status of jobs.
 * @param soap pointer to the SOAP structure
 * @param jobids list of job identifiers
 * @param[out] result status of queried jobs
 * @return SOAP_OK if operation was successfull, error otherwise
 */
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


/**
 * Delete jobs.
 * This function is called by clients to cancel or delete jobs.
 * @param soap pointer to the SOAP structure
 * @param jobids list of job identifiers
 * @param[out] result unused operation result
 * @return SOAP_OK if operation was successfull, error otherwise
 */
int __G3BridgeSubmitter__delete(struct soap *soap, G3BridgeSubmitter__JobIDList *jobids, struct __G3BridgeSubmitter__deleteResponse &result)
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

	/// Iterates through job identifiers.
	for (vector<string>::const_iterator it = jobids->jobid.begin(); it != jobids->jobid.end(); it++)
	{
		auto_ptr<Job> job = dbh->getJob(*it);
		if (!job.get())
		{
			/// Unknown jobs are ignored.
			LOG(LOG_NOTICE, "delete: Unknown job ID %s", it->c_str());
			continue;
		}

		//TODO: cancel downloads, remove files and job???
		job->setStatus(Job::CANCEL);
		LOG(LOG_NOTICE, "Job %s: Cancelled", it->c_str());
	}

	DBHandler::put(dbh);
	return SOAP_OK;
}


/**
 * Get job output URLs.
 * This function is called by clients to get URLs of output files belonging to
 * jobs.
 * @param soap pointer to the SOAP structure
 * @param jobids list of job identifiers
 * @param[out] result list of output URL list
 * @return SOAP_OK if operation was successfull, error otherwise
 */
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


/**
 * Get finished jobs' identifier.
 * The function returns the set of finished job identifiers belonging to a given
 * grid.
 * @param soap pointer to the SOAP structure
 * @param grid the grid or plugin name the user is interested in
 * @param[out] result list of finished jobs' identifiers
 * @return SOAP_OK if operation was successfull, error otherwise
 */
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


/**
 * Get version of the service.
 * @param soap pointer to the SOAP structure
 * @param[out] resp version of the service
 * @return SOAP_OK
 */
int __G3BridgeSubmitter__getVersion(struct soap *soap, std::string &resp)
{
	resp = PACKAGE_STRING;
	return SOAP_OK;
}


/**
 * Get grid data belonging to jobs.
 * @param soap pointer to the SOAP structure
 * @param jobids list of job identifiers
 * @param[out] result list of grid data belonging to specified jobs
 * @return SOAP_OK if operation was successfull, error otherwise
 */
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


int __G3BridgeSubmitter__getGridAlgs(struct soap *soap, string grid, G3BridgeSubmitter__GridAlgList *result)
{
	DBHandler *dbh;

	try
	{
		dbh = DBHandler::get();
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "getGridAlgs: Failed to get a DB handle: %s", e->what());
		delete(e);
		return SOAP_FATAL_ERROR;
	}
	catch (...)
	{
		LOG(LOG_ERR, "getGridAlgs: Failed to get a DB handle: Unknown exception");
		return SOAP_FATAL_ERROR;
	}

	vector<string> algs = dbh->getAlgs(grid);
	result->gridalgs = algs;

	DBHandler::put(dbh);
	return SOAP_OK;
}

/**********************************************************************
 * The SOAP thread handler
 */
/**
 * SOAP service handler.
 * @param data pointer to the SOAP structure
 * @param user_data unused
 */
static void soap_service_handler(void *data, void *user_data)
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
	catch (const exception &ex)
	{
		LOG(LOG_ERR, "SOAP: Caught unhandled exception: %s", ex.what());
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
/**
 * Handler of the INT signal.
 * Instructs WSSubmitter to shut down.
 * @see finish
 */
static void sigint_handler(int signal __attribute__((__unused__)))
{
	finish = true;
}


/**
 * Handler of the HUP signal.
 * Instructs WSSubmitter to reload its configuration.
 * @see reload
 */
static void sighup_handler(int signal __attribute__((__unused__)))
{
	reload = true;
}

/**********************************************************************
 * The main program
 */
/**
 * Main function.
 * @param argc number of command-line arguments
 * @param argv values of command-line arguments
 */
int main(int argc, char **argv)
try
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

	load_path_config(global_config);

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
		if (daemon(0, 0))
		{
			LOG(LOG_CRIT, "Failed to initialize daemon: %s",
			    strerror(errno));
			exit(EXIT_FAILURE);
		}
		pid_file_update();
	}

	try
	{
		DBHandler::init(global_config);
	}
	catch (QMException *e)
	{
		LOG(LOG_ERR, "Fatal: %s", e->what());
		delete e;
		exit(EX_SOFTWARE);
	}

	soap_init2(&soap, SOAP_IO_KEEPALIVE, SOAP_IO_KEEPALIVE);
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

	DBHandler::done();

	g_key_file_free(global_config);
	g_free(input_dir);
	g_free(output_dir);
	g_free(output_url_prefix);

	return EX_OK;
}
catch (const exception &ex)
{
	LOG(LOG_CRIT, "%s", ex.what());
	exit(EXIT_FAILURE);
}
catch (const exception *ex)
{
	LOG(LOG_CRIT, "%s", ex->what());
	exit(EXIT_FAILURE);
}
