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
	GTimeVal when;
	int retries;

public:
	DLItem(const string &URL, const string &path);
	DLItem();
	virtual ~DLItem();
	bool operator<(const DLItem &b);
	bool operator<(const GTimeVal &b);
	bool operator>=(const GTimeVal &b);

	const string &getUrl() const { return url; };
	const string &getPath() const { return path; };

	int getRetries() const { return retries; };
	const GTimeVal getWhen() const { return when; };

	virtual void setRetry(const GTimeVal &when, int retries);
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
