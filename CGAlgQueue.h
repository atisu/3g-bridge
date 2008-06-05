#ifndef __CG_ALGQUEUE_H
#define __CG_ALGQUEUE_H

#include "common.h"

#include <string>
#include <vector>

using namespace std;


/**
 * Algorithm types.
 */
enum CGAlgType {
	CG_ALG_MIN,
	CG_ALG_DB = CG_ALG_MIN,
	CG_ALG_LOCAL,
	CG_ALG_DCAPI,
	CG_ALG_EGEE,
	CG_ALG_MAX
};


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
	CGAlgType getType() { return ttype; }
	string getName() { return tname; }
	unsigned getPackSize() { return mPSize; }
	static CGAlgQueue *getInstance(const CGAlgType &type, const string &algName, const unsigned maxPackSize = 1);
	static void cleanUp();
	vector<processStatistics> *getPStats() { return &pStats; }
	void updateStat(unsigned pSize, unsigned pTime);
	string getStatStr();
    private:
	CGAlgQueue(const CGAlgType &type, const string &algName, const unsigned maxPackSize);
	CGAlgQueue(const CGAlgType &type, const string &algName, const unsigned maxPackSize, const string &statStr);
	CGAlgType ttype;
	string tname;
	unsigned mPSize;
	vector<processStatistics> pStats;
};

#endif  /* __CG_ALGQUEUE_H */
