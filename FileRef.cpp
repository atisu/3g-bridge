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

#include "FileRef.h"

#include <cstring>

#include "Util.h"

FileRef::FileRef(const FileRef &other)
{
	if (this != &other)
	{
		// Deep copy needed
		f_url = string(other.f_url);
		f_size = other.f_size;
		f_md5 = other.f_md5 ? strdup(other.f_md5) : NULL;
	}
}

FileRef::FileRef(const string &url, const char *md5, const off_t size):f_size(size)
{
	/// Copy url into f_url.
	f_url = string(url);

	/// Copy md5 into a newly allocated buffer in f_mdt (assuming md5 is
	/// not NULL).
	f_md5 = md5 ? strdup(md5) : NULL;
}

FileRef::FileRef()
	: f_url(), f_md5(NULL), f_size(-1)
{
}

FileRef::~FileRef()
{
	/// Free any memory allocated in f_md5.
	if (f_md5)
		delete f_md5;
}

FileRef& FileRef::operator=(const FileRef& other)
{
	if (this != &other)
	{
		if (f_md5)
			delete f_md5;

		// Deep copy needed
		f_url = string(other.f_url);
		f_size = other.f_size;
		f_md5 = (other.f_md5 ? strdup(other.f_md5) : NULL);
	}

	return *this;
}

void FileRef::setMD5(const char *md5)
{
	/// Free any memory allocated in f_md5.
	if (f_md5)
		delete f_md5;

	/// Copy md5 into a newly allocated buffer in f_mdt (assuming md5 is
	/// not NULL).
	f_md5 = md5 ? strdup(md5) : NULL;
}

void FileRef::setSize(const off_t size)
{
	/// Set f_size to provided size.
	f_size = size;
}
