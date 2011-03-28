/*
 * Copyright (C) 2009-2011 MTA SZTAKI LPDS
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

package hu.sztaki.lpds.G3Bridge;

/**
 * FileRef Java class.
 * FileRef is used to store file references.
 */
public class FileRef {

	/// URL of the file reference
	private String url;

	/// MD5 hash of the referenced file
	private String md5;

	/// Size of the referenced file
	private int size;

	/**
	 * FileRef constructor.
	 * @param url URL of the referenced file
	 * @param md5 MD5 hash of the referenced file
	 * @param size size of the referenced file
	 */
	public FileRef(String url, String md5, int size) {
		this.url = url;
		this.md5 = md5;
		this.size = size;
	}

	/**
	 * Get URL of referenced file
	 * @return URL of the referenced file
	 */
	public String getURL() {
		return url;
	}

	/**
	 * Get MD5 hash of referenced file
	 * @return MD5 hash of the referenced file
	 */
	public String getMD5() {
		return md5;
	}

	/**
	 * Get size of referenced file
	 * @return size of the referenced file
	 */
	public int getSize() {
		return size;
	}

}
