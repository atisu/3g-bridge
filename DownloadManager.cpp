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

#include <openssl/crypto.h>
#include <curl/curl.h>

/* For OpenSSL multithread support */
static GMutex **ssl_mutexes;

/* libcurl-related variables */
static CURLSH *shared_data;
static GStaticMutex shared_lock = G_STATIC_MUTEX_INIT;

/**********************************************************************
 * libcurl thread-related callbacks for glib
 */

static void lock_curl(CURL *handle, curl_lock_data data, curl_lock_access access, void *userptr)
{
	g_static_mutex_lock(&shared_lock);
}

static void unlock_curl(CURL *handle, curl_lock_data data, void *userptr)
{
	g_static_mutex_unlock(&shared_lock);
}

/**********************************************************************
 * OpenSSL thread-related callbacks for glib
 */

static unsigned long ssl_thread_id(void)
{
	return (unsigned long)g_thread_self();
}

static void ssl_locking(int mode, int n, const char *file G_GNUC_UNUSED, int line G_GNUC_UNUSED)
{
	if (!ssl_mutexes)
	{
		ssl_mutexes = g_new(GMutex *, CRYPTO_num_locks());
		for (int i = 0; i < CRYPTO_num_locks(); i++)
			ssl_mutexes[i] = g_mutex_new();
	}

	if (mode & CRYPTO_LOCK)
		g_mutex_lock(ssl_mutexes[n]);
	else
		g_mutex_unlock(ssl_mutexes[n]);
}

static struct CRYPTO_dynlock_value *ssl_dynlock_create(const char *file G_GNUC_UNUSED, int line G_GNUC_UNUSED)
{
	void *mutex = (void *)g_mutex_new();
	return (struct CRYPTO_dynlock_value *)mutex;
}

static void ssl_dynlock_lock(int mode, struct CRYPTO_dynlock_value *lock, const char *file G_GNUC_UNUSED, int line G_GNUC_UNUSED)
{
	GMutex *mutex = (GMutex *)(void *)lock;

	if (mode & CRYPTO_LOCK)
		g_mutex_lock(mutex);
	else
		g_mutex_unlock(mutex);
}

static void ssl_dynlock_destroy(struct CRYPTO_dynlock_value *lock, const char *file G_GNUC_UNUSED, int line G_GNUC_UNUSED)
{
	g_mutex_free((GMutex *)(void *)lock);
}

/**********************************************************************
 * The download handler function
 */

static size_t write_callback(void *buf, size_t size, size_t items, void *data)
{
	DLItem *item = (DLItem *)data;
	item->write(buf, size * items);
	return size * items;
}

static void download_handler(void *data, void *user_data G_GNUC_UNUSED)
{
	static GStaticPrivate key_curl = G_STATIC_PRIVATE_INIT;
	static GStaticPrivate key_errbuf = G_STATIC_PRIVATE_INIT;
	CURLcode result;
	char *errbuf;
	CURL *curl;

	curl = g_static_private_get(&key_curl);
	if (!curl)
	{
		curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_SHARE, shared_data);
		errbuf = (char *)g_malloc(CURL_ERROR_SIZE);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, (long)TRUE);
		curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10);

		/* XXX For debugging */
		// curl_easy_setopt(curl, CURLOPT_VERBOSE, (long)TRUE);

		g_static_private_set(&key_curl, curl, curl_easy_cleanup);
		g_static_private_set(&key_errbuf, errbuf, g_free);
	}
	errbuf = (char *)g_static_private_get(&key_errbuf);

	DLItem *item = (DLItem *)data;

	curl_easy_setopt(curl, CURLOPT_URL, item->getUrl().c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);

	LOG(LOG_DEBUG, "Starting to download %s", item->getUrl().c_str());

	result = curl_easy_perform(curl);
	if (result)
	{
		LOG(LOG_ERR, "Download failed for %s: %s",
			item->getUrl().c_str(), errbuf);
		/* XXX Handle the error */
	}
	else
	{
		LOG(LOG_DEBUG, "Finished download %s", item->getUrl().c_str());
		/* XXX Check if the job is now ready */
	}
}

/**********************************************************************
 * Class DownloadManager
 */

DownloadManager::DownloadManager(int threads)
{
	pool = g_thread_pool_new(download_handler, NULL, threads, TRUE, NULL);
	if (!pool)
	{
		LOG(LOG_ERR, "Failed to launch download threads");
		exit(1);
	}

	/* Initialize OpenSSL's thread interface */
	CRYPTO_set_id_callback(ssl_thread_id);
	CRYPTO_set_locking_callback(ssl_locking);
	CRYPTO_set_dynlock_create_callback(ssl_dynlock_create);
	CRYPTO_set_dynlock_lock_callback(ssl_dynlock_lock);
	CRYPTO_set_dynlock_destroy_callback(ssl_dynlock_destroy);

	/* Initialize libcurl */
	curl_global_init(CURL_GLOBAL_ALL);
	shared_data = curl_share_init();
	curl_share_setopt(shared_data, CURLSHOPT_LOCKFUNC, lock_curl);
	curl_share_setopt(shared_data, CURLSHOPT_UNLOCKFUNC, unlock_curl);
	curl_share_setopt(shared_data, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);

}

DownloadManager::~DownloadManager()
{
	g_thread_pool_free(pool, TRUE, TRUE);

	curl_share_cleanup(shared_data);
	curl_global_cleanup();

	CRYPTO_set_id_callback(NULL);
	CRYPTO_set_locking_callback(NULL);
	if (ssl_mutexes)
	{
		for (int i = 0; i < CRYPTO_num_locks(); i++)
			g_mutex_free(ssl_mutexes[i]);
		g_free(ssl_mutexes);
	}

}

/**********************************************************************
 * Class DLItem
 */

DLItem::DLItem(const string &URL, const string &path):url(URL),path(path)
{
	fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0640);
	if (fd == -1)
		throw QMException("Failed to create file %s: %s",
			path.c_str(), strerror(errno));
}

DLItem::~DLItem()
{
	if (fd != -1)
		close(fd);
	unlink(path.c_str());
}

size_t DLItem::write(void *buf, size_t size)
{
	return ::write(fd, buf, size);
}
