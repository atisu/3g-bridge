#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "DBHandler.h"
#include "CGAlgQueue.h"

#include <string>
#include <iostream>


using namespace std;


CGAlgQueue::CGAlgQueue(const CGAlgType &type, const string &name, const unsigned maxPackSize):ttype(type),tname(name)
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


CGAlgQueue::CGAlgQueue(const CGAlgType &type, const string &name, const string &statStr):ttype(type),tname(name)
{
	stringstream strstr(statStr);

	strstr >> mPSize;
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


CGAlgQueue *CGAlgQueue::getInstance(const CGAlgType &type, const string &algName, DBHandler *dbH, const unsigned maxPackSize)
{
	for (unsigned i = 0; i < queues.size(); i++)
	        if (queues[i]->getName() == algName && queues[i]->getType() == type)
			return queues[i];

	CGAlgQueue *rv;
	string statStr = dbH->getAlgQStat(type, algName);

	if (statStr == "")
		rv = new CGAlgQueue(type, algName, maxPackSize);
	else
		rv = new CGAlgQueue(type, algName, statStr);

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
