/* -*- mode: c++; coding: utf-8-unix -*-
 *
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

#include <vector>
#include "QMException.h"
#include "DownloadManager.h"
#include "Util.h"
#include "DBHandler.h"
#include "Conf.h"
#include "FileRef.h"

#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/evp.h>

#include <curl/curl.h>

using namespace std;
using namespace dlmgr;
using namespace events;

// Curl & SSL stuff
static void *shared_curl_data = 0;
static GMutex *shared_curl_lock = 0;
static void ssl_init();
static void ssl_done();
static void curl_init();
static void curl_done();

void checkGError(CSTR context, GError *error);
// Core function of the thread pool
static void dlthread(gpointer data, gpointer userdata);

///////////////////////////////////
// DownloadManager
///////////////////////////////////

int DownloadManager::inst_count = 0;

class EPUserData
{
public:
	EventPool *ep;
	DownloadManager *dlmgr;
	EPUserData(EventPool *_ep, DownloadManager *_dlmgr)
		:ep(_ep), dlmgr(_dlmgr) {}
};

DownloadManager::DownloadManager(GKeyFile *config, CSTR instance)
	: instance(instance),
	  num_threads(config::getConfInt(config, instance,
					 "download-threads", 1)),
	  max_retries(config::getConfInt(config, instance,
					 "download-retries", 10)),
	  _aborted(false)
{
	if (num_threads < 1)
		throw new QMException("Invalid thread number (%d) "
				      "specified for plugin '%s'",
				      num_threads, instance);
	if (max_retries < 1)
		throw new QMException("Invalid retry number (%d) "
				      "specified for plugin '%s'",
				      max_retries, instance);
	if (!(inst_count++))
	{
		ssl_init();
		curl_init();
	}
}

DownloadManager::~DownloadManager()
{
	if (!(--inst_count))
	{
		curl_done();
		ssl_done();
	}
}

void DownloadManager::init(EventPool &ep)
{
	GError *error = 0;
	thread_pool = g_thread_pool_new(dlthread, new EPUserData(&ep,this),
					num_threads,
					true, &error);
	checkGError("DownloadManager::init", error);
	LOG(LOG_DEBUG,
	    "DownloadManager '%s' is initialized. "
	    "num_threads=%d; max_retries=%d",
	    instance.c_str(), num_threads, max_retries);

	loadDB();

	ep[EventNames::DLRequested] += *this;
	ep[CommonEvents::JobCancelled] += *this;
	ep[CommonEvents::ProcessExiting] += *this;
}
static void addDLItem(void * user_data, CSTR jobid, CSTR localname, CSTR url,
		      const struct timeval *, int)
{
	DownloadManager *dlmgr = reinterpret_cast<DownloadManager*>(user_data);
	dlmgr->queueItem(jobid, localname, url);
}
void DownloadManager::loadDB()
{
	DBHWrapper dbh;
	dbh->getAllDLs(addDLItem, this);
}
void DownloadManager::handle(const string &eventName, EventData *data)
{
	LOG(LOG_DEBUG, "[DlMgr] Handling event '%s'", eventName.c_str());
	if (eventName == EventNames::DLRequested)
		dladded(check<DLEventData>(eventName, data));
	else if (eventName == CommonEvents::JobCancelled)
		dlcancelled(check<JobEventData>(eventName, data));
	else if (eventName == CommonEvents::ProcessExiting)
		quit();
	else
		LOG(LOG_DEBUG, "[DlMgr] Unknown event to handle: '%s'", eventName.c_str());
}
void DownloadManager::queueItem(const string &jobId, const string &logicalFile,
				const string &URL)
{
	DLItem *nitem = &DLItem::getNew(jobId, logicalFile, URL,
					config::calc_input_path(jobId,
								logicalFile));
	GError *err = 0;
	g_thread_pool_push(thread_pool, nitem, &err);
	checkGError("DownloadManager::thread_pool_push()", err);
}
void DownloadManager::dladded(DLEventData *data)
{
	typedef vector<string> fnList;

	DBHWrapper dbh;
	const DLException &ex = *data->get();
	const string &jobId = ex.getJobId();
	auto_ptr<Job> job = dbh->getJob(jobId);

	CriticalSection cs(_jobLock[jobId]);

	job->setStatus(Job::PREPARE);
	
	const fnList &lst = ex.getInputs();
	for (fnList::const_iterator i = lst.begin(); i != lst.end(); i++)
	{
		FileRef fr = job->getInputRef(*i);
		const string &URL = fr.getURL();
		LOG(LOG_DEBUG,
		    "[DlMgr] Processing download request "
		    "for job '%s': '%s'->'%s'",
		    jobId.c_str(), URL.c_str(), i->c_str());
		dbh->addDL(jobId, *i, URL);
		queueItem(jobId, *i, URL);
	}

	data->setHandled(true);
}
void DownloadManager::dlcancelled(JobEventData *data)
{
	if (DLItem::cancelJobDownloads(data->get()->getId()))
		LOG(LOG_DEBUG, "[DlMgr] Canceled downloads for job %s",
		    data->get()->getId().c_str());
}
void DownloadManager::quit()
{
	LOG(LOG_DEBUG, "[DlMgr] Aborting downloads...");
	_aborted = true;
	g_thread_pool_free(thread_pool, TRUE, true); //TODO: (_wait) true?
	LOG(LOG_DEBUG, "[DlMgr] Downloads aborted");
}

void checkGError(CSTR context, GError *error)
{
	if (error != 0)
	{
		QMException *ex =new QMException("glib error: %s: %s",
						 context, error->message);
		g_error_free(error);
		throw ex;
	}
}

struct CallbackData
{
	DownloadManager &parent;
	DLItem &item;
	CallbackData(DownloadManager &_parent,
		     DLItem &_item)
		: parent(_parent), item(_item)
	{}
};

static size_t write_callback(void *buf, size_t size, size_t items, void *data)
{
	CallbackData *d = reinterpret_cast<CallbackData *>(data);
	return d->item.write(buf, size * items);
}
static int progress_callback(void *clientp,
			     double, double,
			     double, double)
{
	CallbackData *d = reinterpret_cast<CallbackData *>(clientp);
	return d->item.cancelled() || d->parent.aborted();
}

static void dlthread(gpointer data, gpointer userdata)
try
{
	EPUserData *ud = reinterpret_cast<EPUserData*>(userdata);
	DownloadManager &parent = *ud->dlmgr;
	EventPool &ep = *ud->ep;
	DLItem &item = *reinterpret_cast<DLItem*>(data);
	CSTR_C s_lf = item.logicalFile().c_str();
	CSTR_C s_jid = item.jobId().c_str();
	CSTR_C s_url = item.url().c_str();
	string dlerror;
	bool finished = false, failed = false;

	LOG(LOG_DEBUG, "[DlMgr] Starting to download '%s' for job '%s' from '%s'",
	    s_lf, s_jid, s_url);

	CURL *curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_SHARE, shared_curl_data);
	auto_ptr<char> errbuf(reinterpret_cast<char*>(g_malloc(CURL_ERROR_SIZE)));
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf.get());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_callback);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)TRUE);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, (long)TRUE);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10);

	curl_easy_setopt(curl, CURLOPT_URL, item.url().c_str());
	CallbackData cbdata(parent, item);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cbdata);
	curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, &cbdata);

	CURLcode result;
	try { result = curl_easy_perform(curl); }
	catch (const QMException *ex)
	{
		dlerror = MKStr() << "Birdge error: " << ex->what();
	}

	long http_response;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response);

	if (parent.aborted())
	{
		LOG(LOG_DEBUG,
		    "[DlMgr] DownloadManager exiting. Download aborted.");
	}
	else
	{
		
		// DBHWrapper scope
		DBHWrapper dbh;
		CriticalSection cs(parent.jobLock()[item.jobId()]);
		dbh->deleteDL(item.jobId(), item.logicalFile());
		auto_ptr<Job> job = dbh->getJob(item.jobId());
		JobEventData ed(job.get());
		
		if (item.cancelled())
		{
			//Remove files
			LOG(LOG_INFO,
			    "[DlMgr] Download of '%s' for "
			    "job '%s' has been cancelled.",
			    s_lf, s_jid);
		}
		else
		{
			if (http_response >= 400 && http_response < 600)
				//TODO: retry if code==5xx
			{
				dlerror = MKStr()
					<< "HTTP code " << http_response;
			}
			if (dlerror.empty())
			{
				try
				{
					item.finished(dbh);
					LOG(LOG_INFO,
					    "[DlMgr] Download of '%s' for job "
					    "'%s' has finished successfully.",
					    s_lf, s_jid);
					finished = true;
				}
				catch (const QMException *ex)
				{
					dlerror = MKStr()
						<< "Bridge error: "
						<< ex->what();
				}
			}			
			if (!dlerror.empty())
			{
				const string & msg =
					MKStr() << "Downloading file '"
						<< item.logicalFile()
						<< "' from URL '" << item.url()
						<< "' has failed: "
						<< dlerror;
				LOG(LOG_INFO, "[DlMgr] %s", msg.c_str());
				item.failed(dbh, msg);
				failed = true;
			}

			if (finished)
				ep[EventNames::DLFinished](&ed);
			if (failed)
			{
				// Cancel other downloads
				ep[CommonEvents::JobCancelled](&ed);
				ep[EventNames::DLFailed](&ed);
			}
		} // cancelled/!cancelled
	} // aborted/!aborted
	
	DLItem::remove(item);

	curl_easy_cleanup(curl);
	ERR_remove_state(0);
}
catch (const exception &ex)
{
	LOG(LOG_CRIT, "[DlMgr] %s", ex.what());
}
catch (const exception *ex)
{
	LOG(LOG_CRIT, "[DlMgr] %s", ex->what());
}

/**********************************************************************
 * Curl related functions
 */

static void lock_curl(CURL *handle, curl_lock_data data,
		      curl_lock_access access, void *userptr)
{
	g_mutex_lock(reinterpret_cast<GMutex *>(userptr));
}

static void unlock_curl(CURL *handle, curl_lock_data data, void *userptr)
{
	g_mutex_unlock(reinterpret_cast<GMutex *>(userptr));
}

static void curl_init()
{
	LOG(LOG_DEBUG, "[DlMgr] Initializing Curl");
	curl_global_init(CURL_GLOBAL_ALL);
	shared_curl_data = curl_share_init();
	shared_curl_lock = g_mutex_new();
	curl_share_setopt(shared_curl_data, CURLSHOPT_USERDATA, shared_curl_lock);
	curl_share_setopt(shared_curl_data, CURLSHOPT_LOCKFUNC, lock_curl);
	curl_share_setopt(shared_curl_data, CURLSHOPT_UNLOCKFUNC, unlock_curl);
	curl_share_setopt(shared_curl_data, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
}

void curl_done()
{
	LOG(LOG_DEBUG, "[DlMgr] Cleaning up Curl");
	curl_share_cleanup(shared_curl_data);
	g_mutex_free(shared_curl_lock);
	curl_global_cleanup();
}


/**********************************************************************
 * SSL related functions
 */

static GStaticMutex *ssl_mutexes = 0;

static unsigned long thread_id_ssl(void)
{
	return (unsigned long)g_thread_self();
}

static void lock_ssl(int mode, int n, const char *file G_GNUC_UNUSED, int line G_GNUC_UNUSED)
{
	if (mode & CRYPTO_LOCK)
		g_static_mutex_lock(&ssl_mutexes[n]);
	else
		g_static_mutex_unlock(&ssl_mutexes[n]);
}

static struct CRYPTO_dynlock_value *create_ssl_lock(const char *file G_GNUC_UNUSED,
						    int line G_GNUC_UNUSED)
{
	return reinterpret_cast<struct CRYPTO_dynlock_value *>(g_mutex_new());
}

static void lock_ssl_lock(int mode, struct CRYPTO_dynlock_value *lock,
			  const char *file G_GNUC_UNUSED, int line G_GNUC_UNUSED)
{
	GMutex *mutex = reinterpret_cast<GMutex *>(lock);

	if (mode & CRYPTO_LOCK)
		g_mutex_lock(mutex);
	else
		g_mutex_unlock(mutex);
}

static void destroy_ssl_lock(struct CRYPTO_dynlock_value *lock,
			     const char *file G_GNUC_UNUSED,
			     int line G_GNUC_UNUSED)
{
	g_mutex_free(reinterpret_cast<GMutex *>(lock));
}

static void ssl_init()
{
	LOG(LOG_DEBUG, "[DlMgr] Initializing SSL");
	ssl_mutexes = g_new(GStaticMutex, CRYPTO_num_locks());
	for (int i = 0; i < CRYPTO_num_locks(); i++)
		g_static_mutex_init(&ssl_mutexes[i]);

	CRYPTO_set_id_callback(thread_id_ssl);
	CRYPTO_set_locking_callback(lock_ssl);
	CRYPTO_set_dynlock_create_callback(create_ssl_lock);
	CRYPTO_set_dynlock_lock_callback(lock_ssl_lock);
	CRYPTO_set_dynlock_destroy_callback(destroy_ssl_lock);
}

static void ssl_done()
{
	LOG(LOG_DEBUG, "[DlMgr] Cleaning up SSL");
	
	ERR_remove_state(0);
	ERR_free_strings();

	ENGINE_cleanup();
	EVP_cleanup();

	CONF_modules_finish();
	CONF_modules_free();
	CONF_modules_unload(1);

	CRYPTO_cleanup_all_ex_data();

	CRYPTO_set_id_callback(NULL);
	CRYPTO_set_locking_callback(NULL);
	CRYPTO_set_dynlock_create_callback(NULL);
	CRYPTO_set_dynlock_lock_callback(NULL);
	CRYPTO_set_dynlock_destroy_callback(NULL);

	if (ssl_mutexes)
		g_free(ssl_mutexes);
}

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new DownloadManager(config, instance);
}
