#ifndef QUEUEMANAGER_H
#define QUEUEMANAGER_H

#include "Job.h"
#include "AlgQueue.h"
#include "GridHandler.h"

#include <glib.h>
#include <map>
#include <set>
#include <string>
#include <vector>


using namespace std;


/**
 * Queue Manager. This is the main class of the bridge responsible for
 * periodically calling different GridHandler plugins to performs the
 * operations on the jobs in the database.
 */
class QueueManager {
public:
	/**
	 * Constructor. Sets up GridHandler plugins defined in the
	 * configuration file.
	 * @param config configuration file object
	 */
	QueueManager(GKeyFile *config);

	/**
	 * Destuctor. Deletes every GridHandler plugin and algorithm queue.
	 */
	~QueueManager();

	/**
	 * Main loop. Periodically calls the runHandler function for every
	 * registered GridHandler plugin. Stops when there is no more work
	 * to do or the file "stopfile" exists in the working directory.
	 */
	void run();

private:
	/// Vector of usable GridHandler plugins
	vector<GridHandler *> gridHandlers;

	/**
	 * Select batch size. Selects a package size to use for batch
	 * execution based on statistical datas.
	 * @param algQ the algorithm queue to use
	 * @see selectSizeAdv()
	 * @return selected batch size.
	 */
	unsigned selectSize(AlgQueue *algQ);

	/**
	 * Advanced select batch size. Selects a package size to use for batch
	 * execution based on statistical datas. This is an improved version
	 * of selectSize.
	 * @param algQ the algorithm queue to use
	 * @see selectSize()
	 * @return selected batch size.
	 */
	unsigned selectSizeAdv(AlgQueue *algQ);

	/**
	 * Run a GridHandler plugin. First, selects some of the new jobs
	 * belonging to the plugin for submission. Next, asks the plugin to
	 * update the status of jobs belonging to it.
	 * @param handler pointer to the GridHandler plugin
	 * @return true if some work has been performed, false otherwise
	 */
	bool runHandler(GridHandler *handler);
};

#endif  /* QUEUEMANAGER_H */
