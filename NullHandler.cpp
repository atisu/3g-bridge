/*
 * Copyright (C) 2008 MTA SZTAKI LPDS
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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "DBHandler.h"
#include "NullHandler.h"
#include "GridHandler.h"
#include "Job.h"
#include "Util.h"

using namespace std;


NullHandler::NullHandler(GKeyFile *config, const char *instance) throw (BackendException *)
{
	name = instance;
	groupByNames = false;
	LOG(LOG_INFO, "NULL Handler: instance \"%s\" initialized.", name.c_str());
}


NullHandler::~NullHandler()
{
}


void NullHandler::submitJobs(JobVector &jobs) throw (BackendException *)
{
	if (!jobs.size())
		return;
	for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
	{
		Job *actJ = *it;
		actJ->setStatus(Job::RUNNING);
	}
	LOG(LOG_DEBUG, "NULL Handler (%s): set %zd jobs' status to RUNNING.",
		name.c_str(), jobs.size());
}


void NullHandler::updateStatus(void) throw (BackendException *)
{
	DBHandler *jobDB = DBHandler::get();
	jobDB->pollJobs(this, Job::RUNNING, Job::CANCEL);
	DBHandler::put(jobDB);
}


void NullHandler::poll(Job *job) throw (BackendException *)
{
	switch (job->getStatus())
	{
		case Job::RUNNING:
			job->setStatus(Job::FINISHED);
			LOG(LOG_DEBUG, "NULL Handler (%s): set status of job \"%s\" to FINISHED.",
				name.c_str(), job->getId().c_str());
			break;
		default:
			break;
	}
}


GridHandler *NullHandler::getInstance(GKeyFile *config, const char *instance)
{
	return new NullHandler(config, instance);
}
