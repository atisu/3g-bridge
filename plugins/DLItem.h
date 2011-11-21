/* -*- mode: c++; coding: utf-8-unix -*-
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

#ifndef __DLITEM_H
#define __DLITEM_H

#include "DBHandler.h"
#include <glib.h>
#include <string>
#include <map>
#include <exception>
#include "mkstr"

using std::string;
using std::map;

class ItemNotFound : public std::exception
{
	const string msg;
public:
	ItemNotFound(const string &jobid, const string &logicalName)
		: msg(MKStr() << "No DLItem with jobid '"
		      << jobid << "' and logicalName '"
		      << logicalName << "' exists.")
	{}
	virtual ~ItemNotFound() throw () {}
	virtual const char * what() const throw() { return msg.c_str(); }
};

class ItemExists : public std::exception
{
	const string msg;
public:
	ItemExists(const string &jobid, const string &logicalName)
		: msg(MKStr() << "DLItem already exists for job '"
		      << jobid << "' and logicalName '"
		      << logicalName << "'")
	{}
	virtual ~ItemExists() throw () {}
	virtual const char * what() const throw() { return msg.c_str(); }
};

/**
 * DLItem class.
 * The DLItem class is used to represent a download item of the Download
 * Manager.
 */
class DLItem
{
private:
	typedef map<string, DLItem*> filename_item_map;
	typedef map<string, filename_item_map > ItemMap;
	static ItemMap _instances;

	const string _jobId;
	const string _logicalFile;
	/// File descriptor of the DLItem
	int _fd;
	/// Temporary file path of the DLItem
	string tmp_path;
	/// URL of the DLItem
	string _url;
	/// Expected path of the downloaded file
	string _path;
	/// Indicator for cancelling the DLItem's download
	bool _cancelled;

	/**
	 * DLItem constructor.
	 * The constuctor creates a new DLItem object, representing a file
	 * download item from url to path.
	 * @see url
	 * @see path
	 * @param url the URL to download
	 * @param path the path where the file should be stored
	 */
	DLItem(const string &jobId, const string &logicalFile,
	       const string &URL, const string &path)
		: _jobId(jobId), _logicalFile(logicalFile), _fd(-1), 
		  _url(URL), _path(path), _cancelled(false)
	{}

public:
	static DLItem &get(const string &jobId, const string &logicalFile)
		throw (ItemNotFound);
	static DLItem &getNew(const string &jobId, const string &logicalFile,
			      const string &URL, const string &path)
		throw (ItemExists);
	static void cancelJobDownloads(const string &jobId);
	static void remove(DLItem &item);

	virtual ~DLItem();

	const string &jobId() const { return _jobId; }
	const string &logicalFile() const { return _logicalFile; }
	const string &url() const { return _url; };
	const string &path() const { return _path; };

	size_t write(void *buf, size_t size);

	virtual void finished(DBHWrapper &dbh);
	virtual void failed(DBHWrapper &dbh, const string &msg);
	virtual void cancel();
	bool cancelled() const { return _cancelled; };
};

#endif //__DLITEM_H
