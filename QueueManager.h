/*
 * Copyright (C) 2008 MTA SZTAKI LPDS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * In addition, as a special exception, MTA SZTAKI gives you permission to link the
 * code of its release of the 3G Bridge with the OpenSSL project's "OpenSSL"
 * library (or with modified versions of it that use the same license as the
 * "OpenSSL" library), and distribute the linked executables. You must obey the
 * GNU General Public License in all respects for all of the code used other than
 * "OpenSSL". If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version.
 */
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
