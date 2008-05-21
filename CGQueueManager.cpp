#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Logging.h"
#include "CGQueueManager.h"

#include <map>
#include <list>
#include <string>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

#ifdef HAVE_DCAPI
#include "DCAPIHandler.h"
#endif
#ifdef HAVE_EGEE
#include "EGEEHandler.h"
#endif

using namespace std;
using namespace mysqlpp;


/**
 * Constructor. Initialize selected grid plugin, and database connection.
 */
CGQueueManager::CGQueueManager(QMConfig &config)
{
	// Clear algorithm list
	algs.clear();

	jobDB = new DBHandler(config);
  
#ifdef HAVE_DCAPI
	gridHandlers[CG_ALG_DCAPI] = new DCAPIHandler(jobDB, config);
#endif
#ifdef HAVE_EGEE
	gridHandlers[CG_ALG_EGEE] = new EGEEHandler(jobDB, config);
#endif
}


/**
 * Destructor. Frees up memory.
 */
CGQueueManager::~CGQueueManager()
{
	for (map<string, CGAlgQueue *>::iterator it = algs.begin(); it != algs.end(); it++)
		delete it->second;

#ifdef HAVE_DCAPI
	delete gridHandlers[CG_ALG_DCAPI];
#endif
#ifdef HAVE_EGEE
	delete gridHandlers[CG_ALG_EGEE];
#endif

	delete jobDB;
	CGAlgQueue::cleanUp();
}


/**
 * Handle jobs. This function handles selected jobs. The performed operation
 * depends on the given op. For successful operations, the job enties in the
 * DB are updated
 *
 * @param[in] op The operation to perform
 * @param[in,out] jobs Pointer to vector of jobs to perform the operation on
 */
void CGQueueManager::handleJobs(jobOperation op, vector<CGJob *> *jobs)
{
	map<CGAlgType, vector<CGJob *> > gridMap;

	// Create a map of algorithm (grid) types to jobs
	if (jobs)
		for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++) {
			CGAlgType actType = (*it)->getAlgQueue()->getType();
			gridMap[actType].push_back(*it);
		}

	// Use the selected grid plugin for handling the jobs
	for (CGAlgType c = CG_ALG_MIN; c != CG_ALG_MAX; c = CGAlgType(c+1)) {
		if (!gridHandlers[c])
    			continue;

		switch (op) {
		case submit:
    			schedReq(gridHandlers[c], &(gridMap[c])); 
    			break;
		case status:
			LOG(LOG_INFO, "Updating status of jobs using grid plugin.");
    			gridHandlers[c]->updateStatus();
    			break;
		case cancel:
    			gridHandlers[c]->cancelJobs(&(gridMap[c]));
    			break;
		}
	}
}


/**
 * Free vectors. This function releases memory space allocated that is not
 * needed anymore.
 *
 * @param[in] what Pointer to vector to free up
 */
void CGQueueManager::freeVector(vector<CGJob *> *what)
{
	for (vector<CGJob *>::iterator it = what->begin(); it != what->end(); it++)
		delete *it;
	delete what;
}


/**
 * Main loop. Periodically queries database for new, sent, finished and
 * aborted jobs. Handler funcitons are called for the different job vectors.
 */
void CGQueueManager::run()
{
	bool finish = false;

	while (!finish) {
		try {
			vector<CGJob *> *newJobs = jobDB->getJobs(INIT);
			vector<CGJob *> *cancelJobs = jobDB->getJobs(CANCEL);

			LOG(LOG_INFO, "Queue Manager found %d new jobs.", newJobs->size());
			handleJobs(submit, newJobs);

			handleJobs(status, 0);

			LOG(LOG_INFO, "Queue Manager found %d jobs to be aborted.", cancelJobs->size());
			handleJobs(cancel, cancelJobs);

			freeVector(newJobs);
			freeVector(cancelJobs);

			//finish = true;

			if (!finish)
				sleep(30);
		} catch (BackendException& a) {
			cout << a.reason() << endl;
		}
	}
}


/**
 * Determine package size. For CancerGrid, "optimal" package sizes are
 * counted here for the different algorithm queues.
 *
 * @param[in] algQ The algorithm queue to use. No other information is
 *                 needed, as the decision depends only on this
 * @return Determined package size
 */
unsigned CGQueueManager::selectSize(CGAlgQueue *algQ)
{
	unsigned maxPSize = algQ->getPackSize();
	double tATT = 0, tVB = 0;
	double vBorder[maxPSize];
	double pBorder[maxPSize];
	double vpBorder[maxPSize + 1];
	vector<processStatistics> *pStats = algQ->getPStats();

	for (unsigned i = 0; i < maxPSize; i++)
		tATT += pStats->at(i).avgTT;

	if (tATT)
    		for (unsigned i = 0; i < maxPSize; i++)
    		{
            		vBorder[i] = (double)1 - pStats->at(i).avgTT/tATT;
            		tVB += vBorder[i];
    		}
	else
	{
    		for (unsigned i = 0; i < maxPSize; i++)
			vBorder[i] = 1;
		tVB = maxPSize;
	}
	

        pBorder[0] = vBorder[0] / tVB;
        vpBorder[0] = 0;
        for (unsigned i = 1; i < maxPSize; i++)
	{
                pBorder[i] = vBorder[i] / tVB;
                vpBorder[i] = vpBorder[i-1] + pBorder[i-1];
        }
        vpBorder[maxPSize] = 1;

        double rand = (double)random() / RAND_MAX;
        for (unsigned i = 0; i < maxPSize; i++)
                if (vpBorder[i] <= rand && rand < vpBorder[i+1])
                    return i+1;

	return 1;
}


/**
 * Determine package size - advanced version. For CancerGrid, "optimal"
 * package sizes are counted here for the different algorithm queues. In
 * this version, the "goodness" of a package size reflects the "smallness"
 * of the package size's average turnaround time better.
 *
 * @param[in] algQ The algorithm queue to use. No other information is
 *                 needed, as the decision depends only on this
 * @return Determined package size
 */
unsigned CGQueueManager::selectSizeAdv(CGAlgQueue *algQ)
{
	unsigned maxPSize = algQ->getPackSize();
	double tATT = 0;
	double pBorder[maxPSize];
	double vpBorder[maxPSize + 1];
	vector<processStatistics> *pStats = algQ->getPStats();

	for (unsigned i = 0; i < maxPSize; i++)
		tATT += (pStats->at(i).avgTT ? 1/pStats->at(i).avgTT : 1);

        pBorder[0] = (pStats->at(0).avgTT ? 1/pStats->at(0).avgTT : 1);
        vpBorder[0] = 0;
        for (unsigned i = 1; i < maxPSize; i++)
	{
                pBorder[i] = (pStats->at(i).avgTT ? 1/pStats->at(i).avgTT : 1);
                vpBorder[i] = vpBorder[i-1] + pBorder[i-1];
        }
        vpBorder[maxPSize] = tATT;

        double rand = (double)random() / RAND_MAX * tATT;
        for (unsigned i = 0; i < maxPSize; i++)
                if (vpBorder[i] <= rand && rand < vpBorder[i+1])
                    return i+1;

	return 1;
}


/**
 * Package submission. For CancerGrid, this implements creating "optimal"
 * size packets and also submits them.
 *
 * @param[in] gh The grid plugin's pointer
 * @param[in] jobs Jobs belonging to an algorithm queue
 */
void CGQueueManager::handlePackedSubmission(GridHandler *gh, vector<CGJob *> *jobs)
{
	CGAlgQueue *algQ = jobs->at(0)->getAlgQueue();
	vector<CGJob *>::iterator it = jobs->begin();
	unsigned maxGSize = gh->schMaxGroupSize();
	unsigned maxASize = algQ->getPackSize();
	unsigned maxSize = (maxGSize < maxASize ? maxGSize : maxASize);

	LOG(LOG_DEBUG, "Packed submission requested, maximum packet size is %d.", maxSize);
	while (it != jobs->end())
	{
		vector<CGJob *> sendJobs;
		unsigned prefSize = selectSizeAdv(algQ);
		unsigned useSize = (prefSize < maxSize ? prefSize : maxSize);
		LOG(LOG_DEBUG, "Scheduler: selected package size is %d.", useSize);

		for (; useSize && it != jobs->end(); useSize--, it++)
			sendJobs.push_back(*it);

		LOG(LOG_DEBUG, "Submitting package of size %d.", sendJobs.size());
		gh->submitJobs(&sendJobs);
	}
}


/**
 * Handle scheduling request. This function is called for every Grid plugin,
 * with a vector of jobs to be submitted to the given grid. Based on the
 * plugin properties, jobs are grouped based on algorithm names.
 * 
 * @param[in] gh The grid plugin's pointer
 * @param[in] jobs Pointer to a vector of jobs to submitted using the grid
 *                 plugin
 */
void CGQueueManager::schedReq(GridHandler *gh, vector<CGJob *> *jobs)
{
	if (!jobs || !jobs->size())
		return;

	LOG(LOG_DEBUG, "Scheduling request received");
	// Query maximum group size of the grid plugin
	unsigned maxGSize = gh->schMaxGroupSize();
	LOG(LOG_DEBUG, "Maximum group size of grid plugin is: %d", maxGSize);

	// Check is jobs should be grouped by algorithm names
	if (!gh->schGroupByNames())
	{
		LOG(LOG_DEBUG, "Grid plugin doesn't request grouping by algorithm names.");
		// If not, simply create maxGSize "packages", and submit
		// the packages
		vector<CGJob *>::iterator it = jobs->begin();

		while (it != jobs->end())
		{
			vector<CGJob *> sendJobs;

			for (unsigned i = 0; i < maxGSize && it != jobs->end(); i++, it++)
				sendJobs.push_back(*it);

			LOG(LOG_DEBUG, "Sending %d jobs to grid plugin for submission.", sendJobs.size());
			gh->submitJobs(&sendJobs);
		}
	}
	else
	{
		LOG(LOG_DEBUG, "Grid plugin requests grouping by algorithm names.");
		// If yes, do the grouping
		map<CGAlgQueue *, vector<CGJob *> > algs2Jobs;
		vector<CGAlgQueue *> algs;

		for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++)
		{
			CGJob *actJ = *it;
			if (!algs2Jobs[actJ->getAlgQueue()].size())
				algs.push_back(actJ->getAlgQueue());
			algs2Jobs[actJ->getAlgQueue()].push_back(actJ);
		}

		// Call "optimal" packet size calculator, that also submits
		// the subgroups
		srandom(time(NULL));
		for (vector<CGAlgQueue *>::iterator it = algs.begin(); it != algs.end(); it++)
			handlePackedSubmission(gh, &(algs2Jobs[*it]));

	}

}
