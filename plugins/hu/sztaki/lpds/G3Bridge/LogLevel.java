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

public final class LogLevel {

	/* The codes must match the C interface */
	private static final int SYSLOG_EMERG = 0;
	private static final int SYSLOG_ALERT = 1;
	private static final int SYSLOG_CRIT = 2;
	private static final int SYSLOG_ERR = 3;
	private static final int SYSLOG_WARNING = 4;
	private static final int SYSLOG_NOTICE = 5;
	private static final int SYSLOG_INFO = 6;
	private static final int SYSLOG_DEBUG = 7;

	/** Names of the log levels. The indexes must match the above codes. */
	private static final String names[] = {
		"EMERGENCY", "ALERT", "CRITICAL", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"
	};

	/**
	 * Debug message.
	 * The message is useful for debugging suspected problems but likely
	 * not useful during normal operation.
	 */
	public static final LogLevel DEBUG = new LogLevel(SYSLOG_DEBUG);

	/**
	 * Informational message.
	 * The message describes a normal and usually insignificant condition.
	 */
	public static final LogLevel INFO = new LogLevel(SYSLOG_INFO);

	/**
	 * Important message.
	 * The message describes an important but normal event.
	 */
	public static final LogLevel NOTICE = new LogLevel(SYSLOG_NOTICE);

	/**
	 * Warning message.
	 * The message describes an exceptional condition that was handled
	 * without loss of functionality.
	 */
	public static final LogLevel WARNING = new LogLevel(SYSLOG_WARNING);

	/**
	 * Error message.
	 * The message desrcibes an error condition that prevents some operation
	 * from being performed or completed, but the overall availability of
	 * the service is not affected.
	 */
	public static final LogLevel ERROR = new LogLevel(SYSLOG_ERR);

	/**
	 * Critical error message.
	 * The message describes a critical condition that affects the general
	 * availability of the service.
	 */
	public static final LogLevel CRITICAL = new LogLevel(SYSLOG_CRIT);

	/**
	 * Alert error message.
	 * The message describes an alert condition that affects the general
	 * availability of the service.
	 */
	public static final LogLevel ALERT = new LogLevel(SYSLOG_ALERT);

	/**
	 * Emergency error message.
	 * The message describes an emergency condition that affects the general
	 * availability of the service.
	 */
	public static final LogLevel EMERG = new LogLevel(SYSLOG_EMERG);

	/** The internal code of the severity object. */
	private int code;

	/**
	 * Creates a new log level object.
	 *
	 * @param code		the internal code of the log level. Must match
	 *			the C code
	 */
	private LogLevel(int code) {
		this.code = code;
	}

	/**
	 * Returns the internal code of the log level.
	 *
	 * @return		the internal code.
	 */
	int getCode() {
		return code;
	}

	/**
	 * Converts a log level to a string.
	 *
	 * @return		the stringified log level.
	 */
	public String toString() {
		return names[code];
	}

	/**
	 * Converts a numeric log level to a string.
	 *
	 * @param level		the numeric level.
	 * @return		the stringified log level.
	 *
	 * @throws IllegalArgumentException if the log level is invalid.
	 */
	public static String toString(Integer level) {
		String name;
		try {
			/* This might throw an OutOfBoundsException */
			name = names[level.intValue()];
		} catch (Exception e) {
			throw new IllegalArgumentException("Unknown log level");
		}
		return name;
	}
}
