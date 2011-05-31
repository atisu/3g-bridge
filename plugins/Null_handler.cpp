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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fstream>
#include <stdlib.h>
#include <time.h>

#include "DBHandler.h"
#include "Null_handler.h"
#include "GridHandler.h"
#include "Job.h"
#include "Util.h"

using namespace std;

char const * const CFG_FAILURE_PERCENT = "failurePercent";

NullHandler::NullHandler(GKeyFile *config, const char *instance) throw (BackendException *): GridHandler(config, instance)
{
	groupByNames = false;
	LOG(LOG_INFO, "NULL Handler: instance \"%s\" initialized.", name.c_str());

	GError *error = 0;
	int value =
		g_key_file_get_integer(config,
				       name.c_str(), CFG_FAILURE_PERCENT,
				       &error);
	if (error)
	{
		value = 0;
		g_error_free(error);
	}

	failure_pct = value;
	srand(time(0));
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
	bool failed = false;
	int r;
	switch (job->getStatus())
	{
	case Job::RUNNING:
		failed = rand()%100 < failure_pct;
		
		if (failed)
		{
			job->setGridData("Null handler made this job fail.");
			job->setStatus(Job::ERROR);
			LOG(LOG_DEBUG,
			    "NULL Handler (%s): set status of job "
			    "\"%s\" to ERROR.",
			    name.c_str(), job->getId().c_str());
		}
		else
		{
			LOG(LOG_DEBUG,
			    "Creating dummy output for job '%s'",
			    job->getId().c_str());
				
			typedef map<string, string>::const_iterator iter;
			typedef map<string, FileRef>::const_iterator inputIter;
			for (iter i = job->getOutputMap().begin();
			     i != job->getOutputMap().end();
			     i++)
			{
				ofstream of(i->second.c_str(),
					    ios::trunc);
				of << "Dummy output file: " << i->first << endl
				   << "Created by Null_handler for job " <<
					job->getId() << endl
				   << "Job arguments: " << job->getArgs() << endl
				   << "Input files:" << endl;
				const map<string, FileRef> &inputRefs =
					job->getInputRefs();
				for (inputIter j = inputRefs.begin();
				     j != inputRefs.end(); j++)
				{
					of << j->first << "=" <<
						j->second.getURL() << endl;
				}
			}
			job->setStatus(Job::FINISHED);
			LOG(LOG_DEBUG,
			    "NULL Handler (%s): set status of job "
			    "\"%s\" to FINISHED.",
			    name.c_str(), job->getId().c_str());
		}
		break;
	case Job::CANCEL:
		DBHWrapper()->deleteJob(job->getId());
		//TODO: remove files
		LOG(LOG_DEBUG,
		    "NULL Handler (%s): DELETED job '%s'",
		    name.c_str(), job->getId().c_str());
		break;
	default:
		break;
	}
}

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new NullHandler(config, instance);
}
