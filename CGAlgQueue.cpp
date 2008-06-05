#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "DBHandler.h"
#include "CGAlgQueue.h"

#include <string>
#include <iostream>
#include <sstream>


using namespace std;

vector<CGAlgQueue *> CGAlgQueue::queues;

CGAlgQueue::CGAlgQueue(const string &grid, const string &name, const unsigned maxPackSize):grid(grid),tname(name)
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
}

CGAlgQueue::CGAlgQueue(const string &grid, const string &name, const unsigned maxPackSize, const string &statStr):grid(grid),tname(name)
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
}


string CGAlgQueue::getStatStr()
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


void CGAlgQueue::cleanUp()
{
	for (unsigned i = 0; i < queues.size(); i++)
	{
		/* TODO: update the database before the delete */
		delete queues[i];
	}
}

/* Load all queue definitions from the database */
void CGAlgQueue::load()
{
	DBHandler *dbh = DBHandler::get();
	dbh->loadAlgQStats();
	DBHandler::put(dbh);
}


CGAlgQueue *CGAlgQueue::getInstance(const string &grid, const string &algName)
{
	for (unsigned i = 0; i < queues.size(); i++)
	        if (queues[i]->getName() == algName && queues[i]->getGrid() == grid)
			return queues[i];
	return NULL;
}

CGAlgQueue *CGAlgQueue::getInstance(const string &grid)
{
	for (unsigned i = 0; i < queues.size(); i++)
	        if (queues[i]->getName() == "" && queues[i]->getGrid() == grid)
			return queues[i];
	return NULL;
}


void CGAlgQueue::updateStat(unsigned pSize, unsigned pTime)
{
	if (pSize >= mPSize)
		return;

	pStats[pSize-1].numPackages++;
	pStats[pSize-1].totalProcessTime += pTime;
	pStats[pSize-1].avgTT = (double)pStats[pSize-1].totalProcessTime / (pSize * pStats[pSize-1].numPackages);
}

vector <CGAlgQueue *> *CGAlgQueue::getAlgs(const string &grid)
{
	vector<CGAlgQueue *> *algs;

	algs = new vector<CGAlgQueue *>;

	for (vector<CGAlgQueue *>::iterator it = queues.begin(); it != queues.end(); it++)
	{
		if ((*it)->getGrid() != grid)
			continue;
		algs->push_back(*it);
	}

	return algs;
}
