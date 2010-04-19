/*
 * Copyright (C) 2009-2010 MTA SZTAKI LPDS
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
 * An <code>RuntimeBridgeException</code> is thrown when the JNI glue code detects
 * an error within itself.
 *
 * @author Gábor Gombás
 * @version $Id$
 */

public class RuntimeBridgeException extends RuntimeException {

	/**
	 * Creates a new <code>RuntimeBridgeException</code> with the
	 * given message.
	 *
	 * @param msg		the message of the exception.
	 */
	public RuntimeBridgeException(String msg) {
		super(msg);
	}

	/**
	 * Creates a new <code>RuntimeBridgeException</code> with the
	 * given message and cause.
	 *
	 * @param msg		the message of the exception.
	 * @param cause		the cause of the exception.
	 */
	public RuntimeBridgeException(String msg, Throwable cause) {
		super(msg, cause);
	}

	/**
	 * Creates a new <code>RuntimeBridgeException</code> with the
	 * given cause.
	 *
	 * @param cause		the cause of the exception.
	 */
	public RuntimeBridgeException(Throwable cause) {
		super(cause);
	}
}
