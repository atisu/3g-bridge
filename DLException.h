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

#ifndef DLEXCEPTION_H
#define DLEXCEPTION_H

#include <exception>
#include <string>
#include <vector>
#include "BackendException.h"

/**
 * Download Exception class.
 * The DLException class can be used by grid plugins during job submission
 * to indicate input file handling problems towards the Queue Manager. If a
 * plugin is unable to handle an input file URL during a job's submission
 * in its submitJobs() method, the plugin has to throw an instance of this
 * exception.
 */
class DLException: public BackendException
{
public:
	/**
	 * Create a new exception using a job ID.
	 * This costructor creates a new DLException using the provided
	 * job identifier.
	 * @see tjobid
	 * @param jobid the job identifier to create the DLException for
	 */
	DLException(const std::string &jobid):tjobid(jobid) {};

	/**
	 * Add input file local name to exception.
	 * This function can be used to add a problematic input's local
	 * name to the exception. The Queue Manager will iterate through
	 * inputs added using this method, and instruct the Download
	 * Manager to download them.
	 * @see localnames
	 * @param lname the problematic file's local name
	 */
	void addInput(const std::string &lname);

	/// Dispose the exception
	~DLException() throw () {};

	/**
	 * Get job identifier.
	 * This function returns the identifier of the job the DLException
	 * belongs to.
	 * @see tjobid
	 * @return the assigned job's identifier
	 */
	const std::string getJobId() const { return tjobid; };

	/**
	 * Get problematic input list.
	 * This function returns the list of problematic input file local
	 * names.
	 * @see localnames
	 * @return vector of problematic input files' local names
	 */
	const std::vector<std::string> &getInputs() const { return localnames; };

	/**
	 * Return the reason of the exception.
	 * The function returns a textual representation of the DLException.
	 * @return reason message
	 */
	const char *what() const throw ();

protected:
	/// The problematic job's identifier
	std::string tjobid;

	/// The problematic input files' local names
	std::vector<std::string> localnames;
};

#endif /* DLEXCEPTION_H */
