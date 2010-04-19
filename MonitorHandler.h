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

#ifndef MONITORHANDLER_H
#define MONITORHANDLER_H

#include "BackendException.h"

#include <string>
#include <glib.h>

class MonitorHandler {
public:
	/// Initialize GridHandler.
	MonitorHandler() {}

	/**
	 * Constructor using config file and instance name
	 * @param config the config file object
	 * @param instance the name of the instance
	 * @see name
	 */
	MonitorHandler(GKeyFile *config, const char *instance):name(instance) {};

	/// Destructor
	virtual ~MonitorHandler() {}

	/**
	 * Return the number of running jobs.
	 * @return number of jobs
	 */
	virtual unsigned getRunningJobs() throw (BackendException *) = 0;

	/**
	 * Return the number of waiting jobs.
	 * @return number of jobs
	 */
	virtual unsigned getWaitingJobs() throw (BackendException *) = 0;

	/**
	 * Return the number of available CPUs.
	 * @return number of CPUs
	 */
	virtual unsigned getCPUCount() throw (BackendException *) = 0;

protected:
	// Name of the monitor handler instance
	std::string name;
};

/* Factory function prototype */
extern "C" typedef MonitorHandler* (*monitor_factory_func)(GKeyFile *config, const char *instance);

/* Name of the factory symbol in the loadable modules */
#define MONITOR_FACTORY_SYMBOL get_monitor_instance

/* Factory function definition for modules */
#define MONITOR_FACTORY(config, instance) extern "C" MonitorHandler *MONITOR_FACTORY_SYMBOL(GKeyFile *config, const char *instance)

#endif /* MONITORHANDLER_H */
