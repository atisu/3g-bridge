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

#include "DLItem.h"
#include "Util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

DLItem::ItemMap DLItem::_instances;

DLItem::~DLItem()
try
{
	if (_fd != -1)
		close(_fd);
	if (!tmp_path.empty())
		unlink(tmp_path.c_str());
}
catch (...) {}

void DLItem::remove(DLItem &item)
{
	ItemMap::iterator jm = _instances.find(item.jobId());
	if (jm != _instances.end())
		jm->second.erase(item.logicalFile());
	delete &item;
}
DLItem &DLItem::get(const string &jobId, const string &logicalFile)
	throw (ItemNotFound)
{
	ItemMap::iterator jm = _instances.find(jobId);
	if (jm == _instances.end())
		throw ItemNotFound(jobId, logicalFile);
	filename_item_map::iterator t = jm->second.find(logicalFile);
	if (t == jm->second.end())
		throw ItemNotFound(jobId, logicalFile);
	return *(t->second);
}
DLItem &DLItem::getNew(const string &jobId, const string &logicalFile,
		       const string &URL, const string &path)
	throw (ItemExists)
{
	ItemMap::iterator jm = _instances.find(jobId);
	if (jm == _instances.end())
		jm = _instances.insert(
			ItemMap::value_type(jobId,
					    filename_item_map())).first;
	filename_item_map &m = jm->second;
	if (m.find(logicalFile) != m.end())
		throw ItemExists(jobId, logicalFile);

	m[logicalFile] = new DLItem(jobId, logicalFile, URL, path);
	return *m[logicalFile];
}

void DLItem::cancelJobDownloads(const string &jobId)
{
	ItemMap::iterator jm = _instances.find(jobId);
	if (jm != _instances.end())
	{
		filename_item_map &m = jm->second;
		for (filename_item_map::iterator i = m.begin();
		     i != m.end(); i++)
		{
			i->second->cancel();
		}
	}
}

size_t DLItem::write(void *buf, size_t size)
{
	if (_fd < 0)
	{
		char buf[PATH_MAX];
		size_t dirlen = _path.find_last_of('/');
		CSTR_C pth = _path.c_str();
		
		if (dirlen == string::npos)
			snprintf(buf, sizeof(buf), ".%s_XXXXXX", pth);
		else
			snprintf(buf, sizeof(buf), "%.*s.%s_XXXXXX",
				 (int)dirlen + 1,
				 pth, pth + dirlen + 1);
		_fd = mkstemp(buf);
		if (_fd == -1)
			throw new QMException("Failed to create a temporary file: %s",
				strerror(errno));
		tmp_path = buf;

	}
	return ::write(_fd, buf, size);
}

void DLItem::finished(DBHWrapper &dbh)
{
	if (_fd >= 0)
	{
		fchmod(_fd, 0644);
		close(_fd);
		_fd = -1;
	}
	LOG(LOG_DEBUG, "Renaming input file '%s' -> '%s'",
	    tmp_path.c_str(), _path.c_str());
	if (rename(tmp_path.c_str(), _path.c_str()))
	{
		throw new QMException("Failed to rename '%s' to '%s': %s",
				      tmp_path.c_str(),
				      _path.c_str(),
				      strerror(errno));
	}
	
	dbh->updateInputPath(_jobId, _logicalFile, _path);
	auto_ptr<Job> job = dbh->getJob(jobId());
	if (0 == dbh->getDLCount(jobId()))
	{
		job->setStatus(Job::INIT);
		LOG(LOG_INFO, "Job downloads completed for job '%s'",
		    job->getId().c_str());
	}
}
void DLItem::failed(DBHWrapper &dbh, const string &msg)
{
	auto_ptr<Job> job = dbh->getJob(jobId());
	job->setStatus(Job::ERROR);
	job->setGridData(msg);
	LOG(LOG_INFO, "Job download failed for job '%s'",
	    job->getId().c_str());
}
void DLItem::cancel()
{
	_cancelled = true;
}
