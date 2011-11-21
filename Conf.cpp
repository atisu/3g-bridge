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
#include <mkstr>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

using std::string;

char *config::input_dir = 0;
char *config::output_dir = 0;
char *config::output_url_prefix = 0;

size_t config::getConfInt(GKeyFile *config, CSTR group, CSTR key, int defVal)
{
	GError *error = 0;
	size_t value =
		g_key_file_get_integer(config, group, key, &error);
	if (error)
	{
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
			throw ConfigException(group, key, error->message);
		else
			value = defVal;
		
		g_error_free(error);
	}

	return value;
}
std::string config::getConfStr(GKeyFile *config, CSTR group, CSTR key, CSTR defVal)
{
	GError *error = 0;
	gchar* value = g_key_file_get_string(config, group, key, &error);
	if (error)
	{
		if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
			throw ConfigException(group, key, error->message);
		else
		{
			if (!defVal)
				throw MissingKeyException(group, key);
			
			return std::string(defVal);
		}
	}

	g_strstrip(value);
	std::string s = value;
	g_free(value);
	return s;
}

config::ConfigException::ConfigException(CSTR_C group, CSTR_C key, CSTR_C reason)
{
	msg = MKStr() << "Config error: ["
		      << group << "]/[" << key << "]: " << reason;
}

static void md(char const *name)
{
	int ret = mkdir(name, 0750);
	if (ret == -1 && errno != EEXIST)
		throw new QMException(
			"Failed to create directory '%s': %s",
			name, strerror(errno));
}

/**
 * Create a hashed directory.
 * This function can be used to create a hash directory for a job with a given
 * identifier on the filesystem.
 * For example, if the job identifier is 48e90506-b44f-4b09-af8b-4ea544eb32ab, a
 * directory base + '/48/48e90506-b44f-4b09-af8b-4ea544eb32ab' is created.
 * @param base the base directory path
 * @param jobid the job identifier to use
 * @param create indicates if the directory should be created or not
 * @return full path of the created directory
 */
static string make_hashed_dir(const string &base, const string &jobid, bool create = true)
{
	string dir = base + '/' + jobid.at(0) + jobid.at(1);
	if (create)
		md(dir.c_str());

	dir += '/' + jobid;
	if (create)
		md(dir.c_str());

	return dir;
}

string config::calc_job_path(const string &basedir,
			     const string &jobid)
{
	return make_hashed_dir(basedir, jobid);
}
string config::calc_file_path(const string &basedir,
			      const string &jobid,
			      const string &localName)
{
	return calc_job_path(basedir, jobid) + '/' + localName;
}
string config::calc_input_path(const string &jobid, const string &localName)
{
	return calc_job_path(input_dir, jobid) + '/' + localName;
}
string config::calc_output_path(const string &jobid, const string &localName)
{
	return calc_job_path(output_dir, jobid) + '/' + localName;
}
string config::calc_output_url(const string path)
{
	/* XXX Verify the prefix */
	return (string)output_url_prefix + "/" + path.substr(strlen(output_dir) + 1);
}

void config::load_path_config(GKeyFile *global_config)
{
	input_dir = strdup(getConfStr(global_config,
				      GROUP_WSSUBMITTER, "input-dir").c_str());
	output_dir = strdup(getConfStr(global_config,
				       GROUP_WSSUBMITTER, "output-dir").c_str());
	output_url_prefix = strdup(getConfStr(global_config,
					      GROUP_WSSUBMITTER,
					      "output-url-prefix").c_str());
}
