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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "DownloadManager.h"
#include "QMException.h"
#include "Util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/evp.h>

#include <curl/curl.h>

/**********************************************************************
 * Prototypes
 */

static void *run_dl(void *data G_GNUC_UNUSED);


/**********************************************************************
 * Global variables
 */

/* For OpenSSL multithread support */
static GStaticMutex *ssl_mutexes;

/* For libcurl multithread support */
static void *shared_curl_data;
static GMutex *shared_curl_lock;

static volatile int finish;

static vector<GThread *> threads;

/* Both of these lists are protected by queue_lock */
static GQueue *queue;
static GList *running;

static GMutex *queue_lock;
static GCond *queue_sig;

/* Configuration: max. number of download retries */
static int max_retries;


/**********************************************************************
 * libcurl thread-related callbacks for glib
 */

static void lock_curl(CURL *handle, curl_lock_data data, curl_lock_access access, void *userptr)
{
	g_mutex_lock((GMutex *)userptr);
}

static void unlock_curl(CURL *handle, curl_lock_data data, void *userptr)
{
	g_mutex_unlock((GMutex *)userptr);
}

/**********************************************************************
 * OpenSSL thread-related callbacks for glib
 */

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

static struct CRYPTO_dynlock_value *create_ssl_lock(const char *file G_GNUC_UNUSED, int line G_GNUC_UNUSED)
{
	void *mutex = (void *)g_mutex_new();
	return (struct CRYPTO_dynlock_value *)mutex;
}

static void lock_ssl_lock(int mode, struct CRYPTO_dynlock_value *lock, const char *file G_GNUC_UNUSED, int line G_GNUC_UNUSED)
{
	GMutex *mutex = (GMutex *)(void *)lock;

	if (mode & CRYPTO_LOCK)
		g_mutex_lock(mutex);
	else
		g_mutex_unlock(mutex);
}

static void destroy_ssl_lock(struct CRYPTO_dynlock_value *lock, const char *file G_GNUC_UNUSED, int line G_GNUC_UNUSED)
{
	g_mutex_free((GMutex *)(void *)lock);
}

/**********************************************************************
 * Class DownloadManager
 */

void DownloadManager::init(GKeyFile *config, const char *section)
{
	GError *error = NULL;

	int num_threads = g_key_file_get_integer(config, section, "download-threads", &error);
	if (error)
	{
		if (error->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND)
		{
			num_threads = 4;
			g_error_free(error);
			error = NULL;
		}
		else
			throw new QMException("Failed to parse the number of download threads: %s", error->message);
	}
	if (num_threads <= 0 || num_threads > 1000)
		throw new QMException("Invalid thread number (%d) specified", num_threads);

	max_retries = g_key_file_get_integer(config, section, "download-retries", &error);
	if (error)
	{
		if (error->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND)
		{
			max_retries = 10;
			g_error_free(error);
			error = NULL;
		}
		else
			throw new QMException("Failed to parse the number of retries: %s", error->message);
	}
	if (max_retries <= 0 || max_retries > 100)
		throw new QMException("Invalid retry number (%d) specified", max_retries);

	queue = g_queue_new();
	queue_lock = g_mutex_new();
	queue_sig = g_cond_new();

	/* Initialize OpenSSL's thread interface */
	ssl_mutexes = g_new(GStaticMutex, CRYPTO_num_locks());
	for (int i = 0; i < CRYPTO_num_locks(); i++)
		g_static_mutex_init(&ssl_mutexes[i]);

	CRYPTO_set_id_callback(thread_id_ssl);
	CRYPTO_set_locking_callback(lock_ssl);
	CRYPTO_set_dynlock_create_callback(create_ssl_lock);
	CRYPTO_set_dynlock_lock_callback(lock_ssl_lock);
	CRYPTO_set_dynlock_destroy_callback(destroy_ssl_lock);

	/* Initialize libcurl */
	curl_global_init(CURL_GLOBAL_ALL);
	shared_curl_data = curl_share_init();
	shared_curl_lock = g_mutex_new();
	curl_share_setopt(shared_curl_data, CURLSHOPT_USERDATA, shared_curl_lock);
	curl_share_setopt(shared_curl_data, CURLSHOPT_LOCKFUNC, lock_curl);
	curl_share_setopt(shared_curl_data, CURLSHOPT_UNLOCKFUNC, unlock_curl);
	curl_share_setopt(shared_curl_data, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);

	/* Launch the downloader threads */
	for (int i = 0; i < num_threads; i++)
	{
		GThread *thr = g_thread_create(run_dl, NULL, TRUE, &error);
		if (!thr)
			throw new QMException("Failed to create a new thread: %s", error->message);
		threads.push_back(thr);
	}

}

void DownloadManager::done()
{
	finish = 1;

	/* Wake up all threads so they can exit */
	g_mutex_lock(queue_lock);
	g_cond_broadcast(queue_sig);
	g_mutex_unlock(queue_lock);

	LOG(LOG_DEBUG, "Waiting for downloads to finish");
	while (!threads.empty())
	{
		GThread *thr = threads.front();
		threads.erase(threads.begin());
		g_thread_join(thr);
	}

	/* XXX Remove pending elements from the queue without calling either
	 * ->finished() or ->failed() */
	g_queue_free(queue);
	g_mutex_free(queue_lock);
	g_cond_free(queue_sig);

	curl_share_cleanup(shared_curl_data);
	g_mutex_free(shared_curl_lock);
	curl_global_cleanup();

	/* OpenSSL cleanup. Sigh... */
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

static int sort_by_time(const void *a, const void *b, void *c G_GNUC_UNUSED)
{
	DLItem *aa = (DLItem *)a;
	DLItem *bb = (DLItem *)b;

	return *aa < *bb ? -1 : 1;
}

void DownloadManager::add(DLItem *item)
{
	g_mutex_lock(queue_lock);
	g_queue_insert_sorted(queue, item, sort_by_time, NULL);
	g_cond_signal(queue_sig);
	g_mutex_unlock(queue_lock);
}

static void retry(DLItem *item)
{
	struct timeval t;

	if (item->getRetries() >= max_retries)
	{
		item->failed();
		delete item;
		return;
	}

	gettimeofday(&t, NULL);
	t.tv_sec += (item->getRetries() + 1) * 60;
	item->setRetry(t, item->getRetries() + 1);

	g_mutex_lock(queue_lock);
	g_queue_insert_sorted(queue, item, sort_by_time, NULL);
	g_cond_signal(queue_sig);
	g_mutex_unlock(queue_lock);
}

static size_t write_callback(void *buf, size_t size, size_t items, void *data)
{
	DLItem *item = (DLItem *)data;

	return item->write(buf, size * items);
}

static int find_path(const void *a, const void *b)
{
	const DLItem *item = (const DLItem *)a;
	const string *path = (const string *)b;

	return !(item->getPath() == *path);
}

void DownloadManager::abort(const string &path)
{
	DLItem *item = NULL;
	GList *link;

	g_mutex_lock(queue_lock);

	link = g_list_find_custom(running, &path, find_path);
	if (link)
		((DLItem *)link->data)->abort();
	else
	{
		link = g_queue_find_custom(queue, &path, find_path);
		if (link)
		{
			item = (DLItem *)link->data;
			g_queue_delete_link(queue, link);
		}
	}

	g_mutex_unlock(queue_lock);

	/* ->failed() may want to abort other files so call it outside the lock */
	if (item)
	{
		item->failed();
		delete item;
	}
}

static void check(DLItem *item, long http_response)
{
	if (http_response >= 500 && http_response < 599)
	{
		LOG(LOG_DEBUG, "Download failed for %s, error code %ld (retry)",
			item->getUrl().c_str(), http_response);
		retry(item);
		return;
	}
	if (http_response >= 400 && http_response < 499)
	{
		LOG(LOG_DEBUG, "Download failed for %s, error code %ld",
			item->getUrl().c_str(), http_response);
		item->failed();
		delete item;
		return;
	}

	item->finished();
	delete item;
}

static void *run_dl(void *data G_GNUC_UNUSED)
{
	struct timeval now;
	CURLcode result;
	char *errbuf;
	CURL *curl;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_SHARE, shared_curl_data);
	errbuf = (char *)g_malloc(CURL_ERROR_SIZE);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)TRUE);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, (long)TRUE);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10);

	/* XXX Add support for CURLOPT_SEEKFUNCTION to resume interrupted downloads */

	/* XXX For debugging */
	// curl_easy_setopt(curl, CURLOPT_VERBOSE, (long)TRUE);

	LOG(LOG_DEBUG, "Download thread %p started", g_thread_self());

	g_mutex_lock(queue_lock);

	while (!finish)
	{
		gettimeofday(&now, NULL);

		/* Check if there is an item we can download */
		DLItem *item = (DLItem *)g_queue_peek_head(queue);
		if (!item || *item >= now)
		{
			GTimeVal gnow;

			if (item)
			{
				now = item->getWhen();
				gnow.tv_sec = now.tv_sec;
				gnow.tv_usec = now.tv_usec;
			}
			g_cond_timed_wait(queue_sig, queue_lock, item ? &gnow : NULL);
			continue;
		}

		/* Get the item off the queue and release the queue */
		running = g_list_prepend(running, item);
		g_queue_pop_head(queue);
		g_mutex_unlock(queue_lock);

		curl_easy_setopt(curl, CURLOPT_URL, item->getUrl().c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, item);

		LOG(LOG_DEBUG, "Starting to download %s", item->getUrl().c_str());

		result = curl_easy_perform(curl);

		g_mutex_lock(queue_lock);
		running = g_list_remove(running, item);
		g_mutex_unlock(queue_lock);

		/* Check for the finish flag first and do not call ->failed() if it is set */
		if (finish)
			goto next;
		if (result)
		{
			if (item->isAborted())
			{
				item->failed();
				delete item;
			}
			else
			{
				LOG(LOG_ERR, "Download failed for %s: %s (retry)", item->getUrl().c_str(), errbuf);
				retry(item);
			}
		}
		else
		{
			long http_response;

			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response);
			check(item, http_response);
		}

next:
		g_mutex_lock(queue_lock);
	}

	LOG(LOG_DEBUG, "Download thread %p exiting", g_thread_self());
	g_mutex_unlock(queue_lock);

	curl_easy_cleanup(curl);
	g_free(errbuf);
	ERR_remove_state(0);

	return NULL;
}

/**********************************************************************
 * Class DLItem
 */

DLItem::DLItem(const string &url, const string &path):url(url),path(path)
{
	fd = -1;
	retries = 0;
	memset(&when, 0, sizeof(when));
	tmp_path = NULL;
	aborted = false;
}

DLItem::~DLItem()
{
	if (fd != -1)
		close(fd);
	if (tmp_path)
	{
		unlink(tmp_path);
		g_free(tmp_path);
	}
}

size_t DLItem::write(void *buf, size_t size)
{
	/* Abort the download when shutting down */
	if (aborted || finish)
		return 0;

	if (fd == -1)
	{
		char buf[PATH_MAX];

		string dir;

		size_t dirlen = path.find_last_of('/');
		if (dirlen == string::npos)
			snprintf(buf, sizeof(buf), ".%s_XXXXXX", path.c_str());
		else
			snprintf(buf, sizeof(buf), "%.*s.%s_XXXXXX", (int)dirlen + 1,
				path.c_str(), path.c_str() + dirlen + 1);
		fd = mkstemp(buf);
		if (fd == -1)
			throw new QMException("Failed to create a temporary file: %s",
				strerror(errno));
		tmp_path = g_strdup(buf);
	}
	return ::write(fd, buf, size);
}

void DLItem::finished()
{
	LOG(LOG_DEBUG, "Finished downloading %s", url.c_str());
	if (fd != -1)
	{
		/* Make the file world-readable */
		fchmod(fd, 0644);
		close(fd);
		fd = -1;
	}
	if (rename(tmp_path, path.c_str()))
		throw new QMException("Failed to rename %s to %s: %s",
			tmp_path, path.c_str(), strerror(errno));
	else
	{
		g_free(tmp_path);
		tmp_path = NULL;
	}
}

void DLItem::failed()
{
	LOG(LOG_DEBUG, "Aborted downloading %s", url.c_str());
	if (fd != -1)
	{
		close(fd);
		fd = -1;
	}
	if (path.length())
		unlink(path.c_str());
}

bool DLItem::operator<(const DLItem &b)
{
	return when.tv_sec < b.when.tv_sec ||
		(when.tv_sec == b.when.tv_sec && when.tv_usec < b.when.tv_usec);
}

bool DLItem::operator<(const struct timeval &b)
{
	return when.tv_sec < b.tv_sec ||
		(when.tv_sec == b.tv_sec && when.tv_usec < b.tv_usec);
}

bool DLItem::operator>=(const struct timeval &b)
{
	return when.tv_sec > b.tv_sec ||
		(when.tv_sec == b.tv_sec && when.tv_usec >= b.tv_usec);
}

void DLItem::setRetry(const struct timeval &when, int retries)
{
	this->when = when;
	this->retries = retries;
}
