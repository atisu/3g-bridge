/* -*- mode: c++; coding: utf-8-unix -*-
 *
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

#ifndef CONF_H
#define CONF_H

#include <Util.h>
#include "types.h"

#ifdef __cplusplus

#include <string>
#include <exception>

/*
 * This file holds definitions that are used by multiple components

  @2011-09-16 -- Adam Visegradi
  FIXME: Not quite true. There are LOTS of code duplications in this
  project. When cleaning up, move functions, classes, etc. regarding
  configuration here.
 */

namespace config
{
	class ConfigException : public std::exception
	{
	protected:
		std::string msg;
	public:
		ConfigException(CSTR_C group, CSTR_C key, CSTR_C reason);
		ConfigException(CSTR_C group, CSTR_C key,
				const std::string &reason);
		virtual ~ConfigException() throw() {}
		virtual CSTR what() const throw()
		{
			return msg.c_str();
		}
	};
	class MissingKeyException : public ConfigException
	{
	public:
		MissingKeyException(CSTR_C group,
				    CSTR_C key)
			: ConfigException(group, key, "Missing key") {}
		virtual ~MissingKeyException() throw() {}
	};
	
	/** Gets the given int value from the config. */
	size_t getConfInt(GKeyFile *config, CSTR group, CSTR key, int defVal);
	
        /** Gets the given string value from the config. */
	std::string getConfStr(GKeyFile *config,
			       CSTR group, CSTR key,
			       CSTR defVal = 0);
}

#endif

/* TODO: Move these in the config namespace too. */
/* Special groups in the configuration file */
#define GROUP_DATABASE		"database"
#define GROUP_DEFAULTS		"defaults"
#define GROUP_BRIDGE		"bridge"
#define GROUP_WSSUBMITTER	"wssubmitter"
#define GROUP_WSWATCH		"wswatch"
#define GROUP_WSMONITOR		"wsmonitor"

#endif /* CONF_H */
