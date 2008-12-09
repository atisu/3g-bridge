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

protected:
	string url;
	string path;
	struct timeval when;
	int retries;
	bool aborted;

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
