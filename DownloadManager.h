#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <sys/types.h>
#include <string>
#include <vector>

#include <glib.h>

using namespace std;

class DownloadManager;

class DLItem
{
private:
	string url;
	string path;
	int fd;
	GTimeVal when;
	int retries;
	friend class DownloadManager;

public:
	DLItem(const string &URL, const string &path);
	~DLItem();
	bool operator<(const DLItem &b);
	bool operator<(const GTimeVal &b);
	bool operator>=(const GTimeVal &b);

	const string &getUrl() const { return url; };
	const string &getPath() const { return path; };

	void setRetry(const GTimeVal &when, int retries);
	size_t write(void *buf, size_t size);
	void finished();
	void failed();
};

class DownloadManager
{
private:
	vector<GThread *> threads;

	GQueue *queue;
	GMutex *queue_lock;
	GCond *queue_sig;

	volatile int do_exit;
	int max_retries;

	/* For libcurl multithread support */
	void *shared_curl_data;
	GMutex *shared_curl_lock;

	/* This is the body function of a download thread so it must be static */
	static void *run_dl(void *obj);

	void retry(DLItem *item);

public:
	DownloadManager(int num_threads, int max_retries);
	~DownloadManager();

	void add(DLItem *item);
	void abort(const string &path);
};

#endif /* DOWNLOADMANAGER_H */
