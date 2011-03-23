/*
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

#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <sys/types.h>
#include <string>
#include <vector>

#include <glib.h>

using namespace std;

/**
 * DLItem class.
 * The DLItem class is used to represent a download item of the Download
 * Manager.
 */
class DLItem
{
private:
	/// File descriptor of the DLItem
	int fd;

	/// Temporary file path of the DLItem
	char *tmp_path;

protected:
	/// URL of the DLItem
	string url;

	/// Expected path of the downloaded file
	string path;

	/// Start time of DLItem's download
	struct timeval when;

	/// Number of retries for the DLItem's download
	int retries;

	/// Indicator for cancelling the DLItem's download
	bool aborted;

public:
	/**
	 * DLItem constructor.
	 * The constuctor creates a new DLItem object, representing a file
	 * download item from url to path.
	 * @see url
	 * @see path
	 * @param url the URL to download
	 * @param path the path where the file should be stored
	 */
	DLItem(const string &url, const string &path);

	/**
	 * Virtual DLItem destructor.
	 */
	virtual ~DLItem();

	/**
	 * operator< for DLItem.
	 * This operator can be used to compare the download start time of
	 * the DLItem object to another object's download start time.
	 * @see when
	 * @param b the DLItem object to compare to
	 * @return true, if download of DLItem b has started later
	 */
	bool operator<(const DLItem &b);

	/**
	 * operator< for DLItem.
	 * This operator can be used to compare the download start time of
	 * the DLItem object to a given time.
	 * @see when
	 * @param b the time to compare the download time to
	 * @return true, if the DLItem's download has started before b
	 */
	bool operator<(const struct timeval &b);

	/**
	 * operator>= for DLItem.
	 * This operator can be used to compare the download start time of
	 * the DLItem object to a given time.
	 * @see when
	 * @param b the time to compare the download time to
	 * @return true, if the DLItem's download has started not before b
	 */
	bool operator>=(const struct timeval &b);

	/**
	 * Get DLItem's download URL.
	 * @see URL
	 * @return URL of the DLItem
	 */
	const string &getUrl() const { return url; };

	/**
	 * Get DLItem's download path.
	 * @see path
	 * @return expected path of the DLItem
	 */
	const string &getPath() const { return path; };

	/**
	 * Get DLItem's download retry number.
	 * @see retries
	 * @return number of times the DLItem's download has been tried
	 */
	int getRetries() const { return retries; };

	/**
	 * Get DLItem's download start time.
	 * @see when
	 * @return time when DLItem's download last has been tried
	 */
	const struct timeval getWhen() const { return when; };

	/**
	 * Set DLItem's retry count and download time
	 * @see when
	 * @see retries
	 */
	virtual void setRetry(const struct timeval &when, int retries);

	/**
	 * Write data to file.
	 * This function writes size bytes from buf into the file represented by
	 * file descriptor fd. If fd isn't opened yet, a new temporary file is
	 * created, and its path is stored in tmp_path.
	 * @see fd
	 * @see tmp_path
	 * @param buf the buffer of data
	 * @param size the number of bytes to write from buf
	 * @return number of bytes written to file
	 */
	size_t write(void *buf, size_t size);

	/**
	 * Finish DLItem's download.
	 * This function should be called once a DLItem has been fully
	 * downloaded. The function closes fd, and renames the file tmp_path to
	 * path.
	 * @see fd
	 * @see tmp_path
	 * @see path
	 */
	virtual void finished();

	/**
	 * Indicate download failure.
	 * This function should be called if the download has failed. The
	 * function closes fd, and removes the file located under path.
	 * @see fd
	 * @see path
	 */
	virtual void failed();

	/**
	 * Abort a DLItem's download.
	 * @see aborted
	 */
	void abort(void) { aborted = true; };

	/**
	 * Check if a DLItem has been aborted or not.
	 * @see aborted
	 * @return true if the DLItem has been aborted
	 */
	bool isAborted(void) const { return aborted; };
};

/**
 * DownloadManager.
 * DownloadManager is responsible for downloading jobs' input files from remote
 * URLs.
 */
namespace DownloadManager
{
	/**
	 * Initialize DownloadManager.
	 * This function initializes the DownloadManager. It reads the
	 * configuration file, initializes cURL and SSL libraries, and creates
	 * download threads.
	 * @param config configuration file
	 * @param section section to read configuration from
	 */
	void init(GKeyFile *config, const char *section);

	/**
	 * Terminate DownloadManager.
	 * This function can be used to shut down the DownloadManager. The
	 * function shuts down download threads.
	 */
	void done(void);

	/**
	 * Add a DLItem to DownloadManager.
	 * @param item the DLItem to add
	 */
	void add(DLItem *item);

	/**
	 * Abort a given file's download.
	 * The function can be used to abort the DLItem with the given path.
	 * @param path the destination file path to abort download of
	 */
	void abort(const string &path);
};

#endif /* DOWNLOADMANAGER_H */
