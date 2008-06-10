#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Logging.h"
#include "CGQueueManager.h"
#include "CGManager.h"
#include "DBHandler.h"

#include <map>
#include <list>
#include <string>
#include <iostream>
#include <sstream>

#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

static volatile bool finish = false;

#define STOPFILE_NAME		"stopfile"

static void sigint_handler(int signal __attribute__((__unused__)))
{
	finish = true;
}

/**
 * Constructor. Initialize selected grid plugin, and database connection.
 */
CGQueueManager::CGQueueManager(GKeyFile *config)
{
	char **sections, *handler;
	unsigned i;

	// Clear algorithm list
	algs.clear();

	sections = g_key_file_get_groups(config, NULL);
	for (i = 0; sections && sections[i]; i++)
	{
		/* Skip sections that are not grid definitions */
		if (strncmp(sections[i], "grid:", 5))
			continue;

		handler = g_key_file_get_string(config, sections[i], "handler", NULL);
		if (!handler)
			throw QMException("Handler definition is missing in %s", sections[i]);

		GridHandler *plugin = getPluginInstance(config, handler, sections[i]);
		gridHandlers.push_back(plugin);
		g_free(handler);
	}
	g_strfreev(sections);
}


/**
 * Destructor. Frees up memory.
 */
CGQueueManager::~CGQueueManager()
{
	while (!gridHandlers.empty())
	{
		GridHandler *plugin = gridHandlers.front();
		gridHandlers.erase(gridHandlers.begin());
		delete plugin;
	}
	CGAlgQueue::cleanUp();
}


bool CGQueueManager::runHandler(GridHandler *handler)
{
	bool work_done = false;
	JobVector jobs;

	if (handler->schGroupByNames())
	{
		vector<CGAlgQueue *> algs;
		
		CGAlgQueue::getAlgs(algs, handler->getName());
		for (vector<CGAlgQueue *>::iterator it = algs.begin(); it != algs.end(); it++)
		{

			DBHandler *dbh = DBHandler::get();
			dbh->getJobs(jobs, handler->getName(), (*it)->getName(), INIT, (*it)->getPackSize());
			DBHandler::put(dbh);

			if (!jobs.empty())
			{
				handler->submitJobs(jobs);
				work_done = true;
				jobs.clear();
			}

			dbh = DBHandler::get();
			dbh->getJobs(jobs, handler->getName(), (*it)->getName(), CANCEL, (*it)->getPackSize());
			DBHandler::put(dbh);

			if (!jobs.empty())
			{
				handler->cancelJobs(jobs);
				work_done = true;
				jobs.clear();
			}
		}
	}
	else
	{
		CGAlgQueue *alg = CGAlgQueue::getInstance(handler->getName());

		DBHandler *dbh = DBHandler::get();
		dbh->getJobs(jobs, handler->getName(), INIT, alg->getPackSize());
		DBHandler::put(dbh);

		if (!jobs.empty())
		{
			handler->submitJobs(jobs);
			work_done = true;
			jobs.clear();
		}

		dbh = DBHandler::get();
		dbh->getJobs(jobs, handler->getName(), CANCEL, alg->getPackSize());
		DBHandler::put(dbh);

		if (!jobs.empty())
		{
			handler->cancelJobs(jobs);
			work_done = true;
			jobs.clear();
		}
	}

	handler->updateStatus();

	return work_done;
}

/**
 * Main loop. Periodically queries database for new, sent, finished and
 * aborted jobs. Handler funcitons are called for the different job vectors.
 */
void CGQueueManager::run()
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	while (!finish) {
		struct timeval begin, end, elapsed;

		/* Measure the time needed to maintain the database */
		gettimeofday(&begin, NULL);

		/* Call the runHandler() methods repeatedly until all of them says
		 * there is no more work to do */
		bool work_done;
		do
		{
			work_done = false;
			for (vector<GridHandler *>::const_iterator it = gridHandlers.begin(); it != gridHandlers.end(); it++)
				work_done |= runHandler(*it);

			if (!access(STOPFILE_NAME, R_OK))
				break;
		} while (work_done);

		if (!access(STOPFILE_NAME, R_OK))
			break;

		gettimeofday(&end, NULL);
		timersub(&end, &begin, &elapsed);

		/* Sleep for 5x the time needed to run the body of this loop,
		 * but no more than 5 minutes and no less than 1 sec */
		elapsed.tv_sec *= 5;
		elapsed.tv_usec *= 5;
		elapsed.tv_sec += elapsed.tv_usec / 1000000;
		elapsed.tv_usec = elapsed.tv_usec % 1000000;

		if (elapsed.tv_sec > 300)
			elapsed.tv_sec = 300;
		else if (elapsed.tv_sec < 1)
			elapsed.tv_sec = 1;
		if (elapsed.tv_sec > 5)
			LOG(LOG_DEBUG, "Sleeping for %d seconds", (int)elapsed.tv_sec);
		sleep(elapsed.tv_sec);
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
