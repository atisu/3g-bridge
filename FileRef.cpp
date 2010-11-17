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
#include <iostream>

using namespace std;

FileRef::FileRef(const string &url, const string &md5, const int size):size(size)
{
	this->url = string(url);
	this->md5 = string(md5);
}

/*
FileRef::FileRef(const FileRef &fileref)
{
	this->url = fileref.url;
	this->
	if (fileref.md5)
	{
		cout << "Ez van: " << *(fileref.md5) << endl;
		this->md5 = new string(*(fileref.md5));
	}
	this->size = fileref.size;
}
*/

FileRef::~FileRef()
{
//	if (md5)
//		delete md5;
}

void FileRef::setMD5(const string &md5)
{
	this->md5 = string(md5);
}

void FileRef::setSize(const int size)
{
	this->size = size;
}
