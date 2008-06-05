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
		delete queues[i];
}


CGAlgQueue *CGAlgQueue::getInstance(const string &grid, const string &algName, const unsigned maxPackSize)
{
	for (unsigned i = 0; i < queues.size(); i++)
	        if (queues[i]->getName() == algName && queues[i]->getGrid() == grid)
			return queues[i];

	CGAlgQueue *rv;
	unsigned msize;

	DBHandler *dbH = DBHandler::get();
	string statStr = dbH->getAlgQStat(grid, algName, &msize);
	DBHandler::put(dbH);

	if (statStr == "")
		rv = new CGAlgQueue(grid, algName, msize);
	else
		rv = new CGAlgQueue(grid, algName, msize, statStr);

        queues.push_back(rv);
        return rv;
}


void CGAlgQueue::updateStat(unsigned pSize, unsigned pTime)
{
	if (pSize >= mPSize)
		return;

	pStats[pSize-1].numPackages++;
	pStats[pSize-1].totalProcessTime += pTime;
	pStats[pSize-1].avgTT = (double)pStats[pSize-1].totalProcessTime / (pSize * pStats[pSize-1].numPackages);
}
