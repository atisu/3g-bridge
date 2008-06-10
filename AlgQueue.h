#ifndef ALGQUEUE_H
#define ALGQUEUE_H

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


class AlgQueue {
    public:
	static vector<AlgQueue *> queues;
	const string getGrid() const { return grid; }
	const string getName() const { return tname; }
	unsigned getPackSize() const { return mPSize; }
	static AlgQueue *getInstance(const string &grid, const string &algName);
	static AlgQueue *getInstance(const string &grid);
	static void getAlgs(vector<AlgQueue *> &algs, const string &grid);
	static void cleanUp();
	static void load();
	vector<processStatistics> *getPStats() { return &pStats; }
	void updateStat(unsigned pSize, unsigned pTime);
	string getStatStr();
    protected:
	friend class DBHandler;
	AlgQueue(const string &grid, const string &algName, const unsigned maxPackSize, const string &statStr);
    private:
	AlgQueue(const string &grid, const string &algName, const unsigned maxPackSize);
	string grid;
	string tname;
	unsigned mPSize;
	vector<processStatistics> pStats;
};

#endif  /* ALGQUEUE_H */
