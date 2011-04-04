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

#ifndef FILEREF_H
#define FILEREF_H

#include <string>

using namespace std;

/**
 * File reference class. The FileRef class can be used to store a file reference in 3G Bridge. A file
 * reference consists of the followings: the URL of the file, the MD5 hash of
 * the file, and the size of the file.
 *
 * @author Zoltán Farkas <zfarkas@sztaki.hu>
 */
class FileRef {
public:
	/**
	 * Constructor.
	 * This constructor initializes the FileRef object using the specified
	 * URL, MD5 hash and size information.
	 * @param url the URL of the file reference
	 * @param md5 the MD5 hash of the referenced file
	 * @param size the size of the referenced file
	 * @see f_url
	 * @see f_md5
	 * @see f_size
	 */
	FileRef(const string &url, const char *md5 = NULL, const off_t size = -1);

	/**
	 * Copy constructor.
	 *
	 * This object needs deep copy or else (if only the pointer was copied
	 * by the default copy constructor) copies would try to free the same
	 * memory area (f_md5).
	 */
	FileRef(const FileRef &other);

	/**
	 * Default constructor.
	 * Default empty constructor for the FileRef class.
	 */
	FileRef() : f_url(), f_md5(NULL), f_size(-1) {};

	/**
	 * Destructor.
	 * Destructor for the FileRef class.
	 */
	~FileRef();

	/**
	 * Get file reference's URL.
	 * This function returns the URL belonging to the file reference.
	 * @see f_url
	 * @return URL of the referenced file
	 */
	const string &getURL() const { return f_url; }

	/**
	 * Get referenced file's MD5 hash.
	 * This function returns the MD5 hash belonging to the referenced file.
	 * @see f_md5
	 * @return MD5 hash of the referenced file
	 */
	const char *getMD5() const { return f_md5; }

	/**
	 * Get referenced file's size.
	 * This function returns the size of the referenced file.
	 * @see f_size
	 * @return size of the referenced file
	 */
	const off_t getSize() const { return f_size; }

	/**
	 * Set referenced file's MD5 hash.
	 * This function sets the MD5 hash belonging to the referenced file.
	 * @see f_md5
	 * @param md5 the new MD5 hash to use
	 */
	void setMD5(const char *md5);

	/**
	 * Set referenced file's size.
	 * This function sets the size belonging to the referenced file.
	 * @see size
	 * @param size the new size to use
	 */
	void setSize(const off_t size);

private:
	/**
	 * URL of the referenced file.
	 */
	string f_url;

	/**
	 * MD5 hash of the referenced file.
	 */
	char *f_md5;

	/**
	 * Size of the referenced file.
	 */
	off_t f_size;
};


#endif /* FILEREF_H */
