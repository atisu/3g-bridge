#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <sys/types.h>
#include <string>
#include <vector>

#include <glib.h>

using namespace std;

class DLItem
{
private:
	int fd;

protected:
	string url;
	string path;
	struct timeval when;
	int retries;

public:
	DLItem(const string &URL, const string &path);
	DLItem();
	virtual ~DLItem();
	bool operator<(const DLItem &b);
	bool operator<(const struct timeval &b);
	bool operator>=(const struct timeval &b);

	const string &getUrl() const { return url; };
	const string &getPath() const { return path; };

	int getRetries() const { return retries; };
	const struct timeval getWhen() const { return when; };

	virtual void setRetry(const struct timeval &when, int retries);
	size_t write(void *buf, size_t size);
	virtual void finished();
	virtual void failed();
};

namespace DownloadManager
{
	void init(int num_threads, int max_retries);
	void done(void);

	void add(DLItem *item);
	void abort(const string &path);
};

#endif /* DOWNLOADMANAGER_H */
