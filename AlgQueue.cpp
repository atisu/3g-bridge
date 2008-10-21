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
