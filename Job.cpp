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

#include <map>
#include <string>
#include <cstring>

#include "Job.h"
#include "DBHandler.h"

using namespace std;


Job::Job(const char *id, const char *name, const char *grid, const char *args, JobStatus status, const vector<string> *env):
		id(id),name(name),grid(grid),status(status)
{
	if (args)
		this->args = args;

	// Get algorithm queue instance
	algQ = AlgQueue::getInstance(grid, name);

	// Parse environment
	if (env)
	{
		for (vector<string>::const_iterator it = env->begin();
			it != env->end(); it++)
		{
			string ev = *it;
			size_t epos = ev.find_first_of('=');
			if (epos != ev.npos)
			{
				string att = ev.substr(0, epos);
				string val = ev.substr(epos + 1);
				envs[att] = val;
			}
		}
	}
}


void Job::addInput(const string &localname, const FileRef &fileref)
{
	inputs[localname] = fileref;
}


void Job::addOutput(const string &localname, const string &fsyspath)
{
	outputs[localname] = fsyspath;
}


auto_ptr< vector<string> > Job::getInputs() const
{
	map<string, FileRef>::const_iterator it;
	auto_ptr< vector<string> > rval(new vector<string>);
	for (it = inputs.begin(); it != inputs.end(); it++)
		rval->push_back(it->first);
	return rval;
}


auto_ptr< vector<string> > Job::getOutputs() const
{
	map<string, string>::const_iterator it;
	auto_ptr< vector<string> > rval(new vector<string>);
	for (it = outputs.begin(); it != outputs.end(); it++)
		rval->push_back(it->first);
	return rval;
}


void Job::setGridId(const string &sID)
{
	gridId = sID;

	DBHandler *dbH = DBHandler::get();
	dbH->updateJobGridID(id, gridId);
	DBHandler::put(dbH);
}


void Job::setGridData(const string &sData)
{
	gridData = sData;

	DBHandler *dbH = DBHandler::get();
	dbH->updateJobGridData(id, gridData);
	DBHandler::put(dbH);
}


void Job::setTag(const string &sTag)
{
	tag = new string(sTag);

	DBHandler *dbH = DBHandler::get();
	dbH->updateJobTag(id, sTag);
	DBHandler::put(dbH);
}


void Job::setStatus(JobStatus nStat, bool updateDB)
{
	if (status == nStat)
		return;

	status = nStat;

	if (!updateDB)
		return;

	DBHandler *dbH = DBHandler::get();
	dbH->updateJobStat(id, status);
	DBHandler::put(dbH);
}


void Job::deleteJob()
{
	DBHandler *dbH = DBHandler::get();
	dbH->deleteJob(id);
	DBHandler::put(dbH);
}


auto_ptr< vector<string> > Job::getEnvs() const
{
	map<string, string>::const_iterator it;
	auto_ptr< vector<string> > rval(new vector<string>);
	for (it = envs.begin(); it != envs.end(); it++)
		rval->push_back(it->first);
	return rval;
}


/**
 * Deletes every Job object contained in the JobVector.
 */
void JobVector::clear()
{
	for (JobVector::iterator it = begin(); it != end(); it++)
		delete *it;
	vector<Job *>::clear();
}
