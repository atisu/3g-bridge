#ifndef __CG_ALGQUEUE_H
#define __CG_ALGQUEUE_H

#include "common.h"

#include <string>
#include <vector>

using namespace std;


/**
 * Keep track of number of WUs and  total processing time of a given package
 * size. Used for scheduling.
 */
struct processStatistics {
	unsigned numPackages;		// Number of packages processed
	unsigned totalProcessTime;	// Total processing time
	double avgTT;			// Average TurnAround Time
};


class CGAlgQueue {
    public:
	static vector<CGAlgQueue *> queues;
	string getGrid() { return grid; }
	string getName() { return tname; }
	unsigned getPackSize() { return mPSize; }
	static CGAlgQueue *getInstance(const string &grid, const string &algName, const unsigned maxPackSize = 1);
	static void cleanUp();
	vector<processStatistics> *getPStats() { return &pStats; }
	void updateStat(unsigned pSize, unsigned pTime);
	string getStatStr();
    private:
	CGAlgQueue(const string &grid, const string &algName, const unsigned maxPackSize);
	CGAlgQueue(const string &grid, const string &algName, const unsigned maxPackSize, const string &statStr);
	string grid;
	string tname;
	unsigned mPSize;
	vector<processStatistics> pStats;
};

#endif  /* __CG_ALGQUEUE_H */
