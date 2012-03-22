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
	using std::string;
	
	class ConfigException : public std::exception
	{
		string msg;
	public:
		ConfigException(CSTR_C group, CSTR_C key, CSTR_C reason);
		virtual ~ConfigException() throw() {}
		virtual CSTR what() const throw()
		{
			return msg.c_str();
		}
	};

	class MissingKeyException : public ConfigException
	{
	public:
		MissingKeyException(CSTR_C group, CSTR_C key)
			: ConfigException(group, key, "Missing key") {}
		virtual ~MissingKeyException() throw() {}
	};	
	
	/** Gets the given int value from the config. */
	size_t getConfInt(GKeyFile *config, CSTR group, CSTR key, int defVal);
	
        /** Gets the given string value from the config. */
	string getConfStr(GKeyFile *config,
			  CSTR group, CSTR key,
			  CSTR defVal = 0);


	/**
	 * Load input_dir, output_dir and output_url_prefix. Functions below
	 * depend on this function being called first.
	 */
	void load_path_config(GKeyFile *global_config);

	/**
	 * Path prefix for input files' download.
	 * This variable stores the path of the directory under which jobs' input files
	 * are fetched. The value of this variable is read from the config file.
	 */
	extern char *input_dir;

        /**
	 * Path prefix for produced output files.
	 * This variable stored the path of the directory under which jobs' output files
	 * are placed after jobs have finished. The value of this variable is read from
	 * the config file.
	 * @see output_url_prefix
	 */
	extern char *output_dir;

        /**
	 * Availability URL prefix of output files.
	 * This variable stores the URL prefix where output files can be downloaded
	 * from. The URL stored in this variable should grant access to the path stored
	 * in the output_dir variable. The value of this variable is read from the
	 * config file.
	 * @see output_dir
	 */
	extern char *output_url_prefix;

	string make_hashed_dir(const string &base, const string &jobid,
			       bool create = true);

	string calc_job_path(const string &basedir,
			     const string &jobid);
	string calc_file_path(const string &basedir,
			      const string &jobid,
			      const string &localName);
	/**
	 * Calculate an input file's path.
	 * This function calculates (and creates) the path of a job's input file.
	 * @see input_dir
	 * @param jobid the job's identifier
	 * @param localName the file's local name
	 */
	string calc_input_path(const string &jobid, const string &localName);
	/**
	 * Calculate an output file's path.
	 * This function calculates (and creates) the path of a job's output file.
	 * @see output_dir
	 * @param jobid the job's identifier
	 * @param localName the file's local name
	 */
	string calc_output_path(const string &jobid, const string &localName);
	/**
	 * Calculate the location where an output file can be downloaded from.
	 * @see output_url_prefix
	 * @see output_dir
	 * @param path the file's path on the filesystem
	 * @return the URL through which the file can be downloaded
	 */
	string calc_output_url(const string path);
	
};

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
