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

#ifndef JAVA_HANDLER_H
#define JAVA_HANDLER_H

#include "Job.h"
#include "GridHandler.h"

#include <vector>

#include <jni.h>

using namespace std;

/**
 * Java handler class.
 * Instances of the Java handler can be used as a base for Java implementations
 * of grid plugins.
 */
class JavaHandler: public GridHandler {
public:
	/**
	 * Constructor.
	 * Initializes the Java handler plugin instance.
	 * @param config the configuration file object
	 * @param instance name of the plugin instance
	 * @throws BackendException
	 */
	JavaHandler(GKeyFile *config, const char *instance) throw (BackendException *);

	/**
	 * Destructor.
	 */
	~JavaHandler();

	/**
	 * Submit jobs.
	 * @param jobs vector of jobs to submit
	 * @throws BackendException
	 */
	void submitJobs(JobVector &jobs) throw (BackendException *);

	/**
	 * Update status of jobs.
	 * @throws BackendException
	 */
	void updateStatus(void) throw (BackendException *);

	/**
	 * Poll a job's status.
	 * @param job the job to poll
	 * @throws BackendException
	 */
	void poll(Job *job) throw (BackendException *);

private:
	/// Global reference to the Java gateway object
	jobject objref;

	/// The embedded JVM instance
	JavaVM *jvm;

	/// JNI environment
	JNIEnv *env;
};

#endif /* JAVA_HANDLER_H */
