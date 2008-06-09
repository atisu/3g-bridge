#ifndef __CG_ALGQUEUE_H
#define __CG_ALGQUEUE_H

#include "common.h"

#include <string>
#include <vector>

using namespace std;


class DBHandler;


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
	const string getGrid() const { return grid; }
	const string getName() const { return tname; }
	unsigned getPackSize() const { return mPSize; }
	static CGAlgQueue *getInstance(const string &grid, const string &algName);
	static CGAlgQueue *getInstance(const string &grid);
	static void getAlgs(vector<CGAlgQueue *> &algs, const string &grid);
	static void cleanUp();
	static void load();
	vector<processStatistics> *getPStats() { return &pStats; }
	void updateStat(unsigned pSize, unsigned pTime);
	string getStatStr();
    protected:
	friend class DBHandler;
	CGAlgQueue(const string &grid, const string &algName, const unsigned maxPackSize, const string &statStr);
    private:
	CGAlgQueue(const string &grid, const string &algName, const unsigned maxPackSize);
	string grid;
	string tname;
	unsigned mPSize;
	vector<processStatistics> pStats;
};

#endif  /* __CG_ALGQUEUE_H */
