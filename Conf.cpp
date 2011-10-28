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
/*
  @2011-09-16 by Adam Visegradi
 */

#include "Conf.h"
#include "BackendException.h"
#include "mkstr"

size_t config::getConfInt(GKeyFile *config, CSTR group, CSTR key, int defVal)
{
	GError *error = 0;
	size_t value =
		g_key_file_get_integer(config, group, key, &error);
	if (error)
	{
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
		{
			throw ConfigException(
				group, key,
				MKStr() << "Failed to parse configuration: "
				<< error->message);
		}
		value = defVal;
		g_error_free(error);
	}

	return value;
}
std::string config::getConfStr(GKeyFile *config, CSTR group, CSTR key, CSTR defVal)
{
	GError *error = 0;
	gchar* value =
		g_key_file_get_string(config, group, key, &error);
	if (value)
		g_strstrip(value);
	if (error)
	{
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
		{
			throw ConfigException(
				group, key,
				MKStr() << "Failed to parse configuration: "
				<< error->message);
		}
		if (defVal)
			return std::string(defVal);
		else
			throw MissingKeyException(group, key);
	}

	std::string s = value ? value : std::string();
	g_free(value);
	return s;
}

config::ConfigException::ConfigException(CSTR_C group,
					 CSTR_C key,
					 CSTR_C reason)
{
	msg = MKStr() << "Configuration error: ["
		      << group << "]/[" << key << "]: "
		      << reason;
}
config::ConfigException::ConfigException(CSTR_C group,
					 CSTR_C key,
					 const std::string &reason)
{
	msg = MKStr() << "Configuration error: ["
		      << group << "]/[" << key << "]: "
		      << reason;
}
