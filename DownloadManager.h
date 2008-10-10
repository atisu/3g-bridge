#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <string>

#include <glib.h>

using namespace std;

class DLItem
{
private:
	string url;
	string path;
	int fd;
public:
	DLItem(const string &URL, const string &path);
	~DLItem();

	const string &getUrl() const { return url; };
	const string &getPath() const { return path; };

	size_t write(void *buf, size_t size);
	void finished() {};
	void failed() {};
};

class DownloadManager
{
private:
	GThreadPool *pool;
public:
	DownloadManager(int threads);
	~DownloadManager();

	void queue(DLItem *item);
	void abort(DLItem *item);
};

#endif /* DOWNLOADMANAGER_H */
