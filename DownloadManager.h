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
	char *tmp_path;
	bool aborted;

protected:
	string url;
	string path;
	struct timeval when;
	int retries;

public:
	DLItem(const string &url, const string &path);
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

	void abort(void) { aborted = true; };
	bool isAborted(void) const { return aborted; };
};

namespace DownloadManager
{
	void init(GKeyFile *config, const char *section);
	void done(void);

	void add(DLItem *item);
	void abort(const string &path);
};

#endif /* DOWNLOADMANAGER_H */
