#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "DownloadManager.h"
#include "Logging.h"
#include "QMException.h"

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

/* For OpenSSL multithread support */
static GStaticMutex *ssl_mutexes;

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

DownloadManager::DownloadManager(int num_threads, int max_retries):max_retries(max_retries)
{
	do_exit = 0;

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
	GError *error = NULL;
	for (int i = 0; i < num_threads; i++)
	{
		GThread *thr = g_thread_create(DownloadManager::run_dl, this, TRUE, &error);
		if (!thr)
			throw new QMException("Failed to create a new thread: %s", error->message);
		threads.push_back(thr);
	}

}

DownloadManager::~DownloadManager()
{
	do_exit = 1;

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

void DownloadManager::retry(DLItem *item)
{
	if (item->retries >= max_retries)
	{
		item->failed();
		delete item;
		return;
	}

	g_get_current_time(&item->when);
	g_time_val_add(&item->when, ++item->retries * 10000);

	g_mutex_lock(queue_lock);
	g_queue_insert_sorted(queue, item, sort_by_time, NULL);
	g_cond_signal(queue_sig);
	g_mutex_unlock(queue_lock);
}

static size_t write_callback(void *buf, size_t size, size_t items, void *data)
{
	DLItem *item = (DLItem *)data;

	/* XXX Add support for pause/abort */

	item->write(buf, size * items);
	return size * items;
}

static int find_path(const void *a, const void *b)
{
	const DLItem *item = (const DLItem *)a;
	const string *path = (const string *)b;

	return !(item->getPath() == *path);
}

void DownloadManager::abort(const string &path)
{
	g_mutex_lock(queue_lock);
	GList *link = g_queue_find_custom(queue, &path, find_path);
	if (link)
	{
		DLItem *item = (DLItem *)link->data;
		g_queue_delete_link(queue, link);
		item->failed();
		delete item;
	}
	g_mutex_unlock(queue_lock);
}

void DownloadManager::check(DLItem *item, long http_response)
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

void *DownloadManager::run_dl(void *data)
{
	DownloadManager *self = (DownloadManager *)data;
	CURLcode result;
	GTimeVal now;
	char *errbuf;
	CURL *curl;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_SHARE, self->shared_curl_data);
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

	g_mutex_lock(self->queue_lock);

	while (!self->do_exit)
	{
		g_get_current_time(&now);

		/* Check if there is an item we can download */
		DLItem *item = (DLItem *)g_queue_peek_head(self->queue);
		if (!item || *item >= now)
		{
			if (item)
				now = item->when;
			g_cond_timed_wait(self->queue_sig, self->queue_lock, item ? &now : NULL);
			continue;
		}

		/* Get the item off the queue and release the queue */
		g_queue_pop_head(self->queue);
		g_mutex_unlock(self->queue_lock);

		curl_easy_setopt(curl, CURLOPT_URL, item->getUrl().c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, item);

		LOG(LOG_DEBUG, "Starting to download %s", item->getUrl().c_str());

		result = curl_easy_perform(curl);
		if (result)
		{
			LOG(LOG_ERR, "Download failed for %s: %s (retry)", item->getUrl().c_str(), errbuf);
			self->retry(item);
		}
		else
		{
			long http_response;

			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_response);
			self->check(item, http_response);
		}

		g_mutex_lock(self->queue_lock);
	}

	LOG(LOG_DEBUG, "Download thread %p exiting", g_thread_self());
	g_mutex_unlock(self->queue_lock);

	curl_easy_cleanup(curl);
	g_free(errbuf);
	ERR_remove_state(0);

	return NULL;
}

/**********************************************************************
 * Class DLItem
 */

DLItem::DLItem(const string &URL, const string &path):url(URL),path(path)
{
	fd = -1;
	retries = 0;
	g_get_current_time(&when);
}

DLItem::DLItem()
{
	fd = -1;
	retries = 0;
	g_get_current_time(&when);
}

DLItem::~DLItem()
{
	if (fd != -1)
		close(fd);
}

size_t DLItem::write(void *buf, size_t size)
{
	if (fd == -1)
	{
		fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0640);
		if (fd == -1)
			throw new QMException("Failed to create file %s: %s",
				path.c_str(), strerror(errno));
	}
	return ::write(fd, buf, size);
}

void DLItem::finished()
{
	LOG(LOG_DEBUG, "Finished downloading %s", url.c_str());
	if (fd != -1)
	{
		close(fd);
		fd = -1;
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

bool DLItem::operator<(const GTimeVal &b)
{
	return when.tv_sec < b.tv_sec ||
		(when.tv_sec == b.tv_sec && when.tv_usec < b.tv_usec);
}

bool DLItem::operator>=(const GTimeVal &b)
{
	return when.tv_sec > b.tv_sec ||
		(when.tv_sec == b.tv_sec && when.tv_usec >= b.tv_usec);
}

void DLItem::setRetry(const GTimeVal &when, int retries)
{
	this->when = when;
	this->retries = retries;
}
