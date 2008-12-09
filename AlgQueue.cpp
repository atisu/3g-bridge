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

#include "AlgQueue.h"
#include "DBHandler.h"
#include "Util.h"

#include <string>
#include <iostream>
#include <sstream>


using namespace std;


vector<AlgQueue *> AlgQueue::queues;


AlgQueue::AlgQueue(const string &grid, const string &name, const unsigned maxPackSize):grid(grid),tname(name)
{
	mPSize = (maxPackSize ? maxPackSize : 1);
	pStats.resize(mPSize);
	for (unsigned i = 0; i < mPSize; i++)
	{
		pStats[i].numPackages = 0;
		pStats[i].totalProcessTime = 0;
		pStats[i].avgTT = 0;
	}

	queues.push_back(this);
	LOG(LOG_DEBUG, "Grid %s: added algorithm %s", grid.c_str(), name.length() ? name.c_str() : "*");
}


AlgQueue::AlgQueue(const string &grid, const string &name, const unsigned maxPackSize, const string &statStr):grid(grid),tname(name)
{
	stringstream strstr(statStr);

	mPSize = maxPackSize;
	pStats.resize(mPSize);

	unsigned i = 0;
	for (; i < mPSize && !strstr.eof(); i++)
	{
		strstr >> pStats[i].numPackages;
		strstr >> pStats[i].totalProcessTime;
		pStats[i].avgTT = pStats[i].numPackages ? (double)pStats[i].totalProcessTime / ((i + 1) * pStats[i].numPackages) : 0;
	}
	for (; i < mPSize; i++)
	{
		pStats[i].numPackages = 0;
		pStats[i].totalProcessTime = 0;
		pStats[i].avgTT = 0;
	}

	queues.push_back(this);
	LOG(LOG_DEBUG, "Grid %s: added algorithm %s", grid.c_str(), name.length() ? name.c_str() : "*");
}


string AlgQueue::getStatStr()
{
	stringstream strstr;

	strstr << mPSize << endl;
	for (unsigned i = 0; i < mPSize; i++)
	{
		strstr << pStats[i].numPackages << endl;
		strstr << pStats[i].totalProcessTime << endl;
	}

	return strstr.str();
}


void AlgQueue::cleanUp()
{
	for (unsigned i = 0; i < queues.size(); i++)
	{
		/* TODO: update the database before the delete */
		delete queues[i];
	}
}


/* Load all queue definitions from the database */
void AlgQueue::load()
{
	DBHandler *dbh = DBHandler::get();
	dbh->loadAlgQStats();
	DBHandler::put(dbh);
}


AlgQueue *AlgQueue::getInstance(const string &grid, const string &algName)
{
	for (unsigned i = 0; i < queues.size(); i++)
	        if (queues[i]->getName() == algName && queues[i]->getGrid() == grid)
			return queues[i];
	return NULL;
}


AlgQueue *AlgQueue::getInstance(const string &grid)
{
	for (unsigned i = 0; i < queues.size(); i++)
	        if (queues[i]->getGrid() == grid)
			return queues[i];
	return NULL;
}


void AlgQueue::updateStat(unsigned pSize, unsigned pTime)
{
	if (pSize >= mPSize)
		return;

	pStats[pSize-1].numPackages++;
	pStats[pSize-1].totalProcessTime += pTime;
	pStats[pSize-1].avgTT = (double)pStats[pSize-1].totalProcessTime / (pSize * pStats[pSize-1].numPackages);
}


void AlgQueue::getAlgs(vector<AlgQueue *> &algs, const string &grid)
{
	for (vector<AlgQueue *>::iterator it = queues.begin(); it != queues.end(); it++)
	{
		if ((*it)->getGrid() != grid)
			continue;
		algs.push_back(*it);
	}
}
