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
#ifndef ALGQUEUE_H
#define ALGQUEUE_H


#include <string>
#include <vector>


using namespace std;


class DBHandler;


/**
 * Structure to hold processing statistics. Processing statistics
 * contain information about the numer of packages processed and
 * the total processing time os the packages belonging to a given
 * package size. So there are a number of instances of this structue
 * for an algorithm queue.
 */
struct processStatistics {
	unsigned numPackages;		/**< Number of packages processed */
	unsigned totalProcessTime;	/**< Total processing time */
	double avgTT;			/**< Average TurnAround Time */
};


/**
 * Algorithm Queue class. Job entries in the database can be placed into
 * queues based on: the grid name, and (if the grid supports creating packages
 * of the same algorithm name) algorithm names. However, an algorithm queue
 * isn't a storage of jobs, it is only a property of jobs. An algorithm queue
 * also contains statistical data about how well different package sizes
 * perform.
 */
class AlgQueue {
    public:
	/**
	 * Storage of algorithm queues. All algorithm queues are stored in this
	 * static vector.
	 */
	static vector<AlgQueue *> queues;

	/**
	 * Get the algorithm queue's grid name.
	 * @see grid()
	 * @return the grid's name
	 */
	const string getGrid() const { return grid; }

	/**
	 * Get the algorithm queue's name.
	 * @see tname()
	 * @return the name
	 */
	const string getName() const { return tname; }

	/**
	 * Get the algorithm queue's maximum package size.
	 * @see mPSize()
	 * @return the maximum package size
	 */
	unsigned getPackSize() const { return mPSize; }

	/**
	 * Get the instance of the algorithm queue specified. This function
	 * searches for an algorithm queue that matches the requested
	 * arguments in the static storage.
	 * @param grid name of the grid
	 * @param algName name of the algorithm
	 * @see queues()
	 * @return pointer to the algorithm queue object found or NULL is no
	 *         matching algorithm queue was found
	 */
	static AlgQueue *getInstance(const string &grid, const string &algName);

	/**
	 * Get the instance of the algorithm queue specified. This function
	 * searches for an algorithm queue that matches the requested argument
	 * in the static storage.
	 * @param grid name of the grid
	 * @see queues()
	 * @return pointer to the algorithm queue object found or NULL is no
	 *         matching algorithm queue was found
	 */
	static AlgQueue *getInstance(const string &grid);

	/**
	 * Get set of algorithm queue object pointers matching the specified
	 * grid name.
	 * @param[out] algs reference to a vector of algorithm queue pointers
	 *             to place the matching algorithm queues in
	 * @param grid name of the grid
	 * @see queues()
	 */
	static void getAlgs(vector<AlgQueue *> &algs, const string &grid);

	/**
	 * Clean up algorithm queue instances. Deletes algorithm queue objects
	 * in the static storage, and updates processing statistics in the
	 * database.
	 * @see queues()
	 */
	static void cleanUp();

	/**
	 * Load algorithm queues. Loads algorithm queue instances from the
	 * database into the static storage.
	 * @see queues()
	 */
	static void load();

	/**
	 * Get processing statistics for thr algorithm queue.
	 * @return pointer to a vector containing processing statistics
	 *         information
	 */
	vector<processStatistics> *getPStats() { return &pStats; }

	/**
	 * Update processing statistics. Increases the processing time of the
	 * given package size.
	 * @param pSize the package size
	 * @param pTime the processing time
	 */
	void updateStat(unsigned pSize, unsigned pTime);

	/**
	 * Returns a string representation of processing statistics information.
	 * The first line of the string contains the number of package sizes.
	 * Afterwards, every two lines contain the following informations for
	 * increasing package sizes: the number of packages processed, and the
	 * total processing time for the given package size.
	 * @return string representation of processing statistics
	 */
	string getStatStr();

    protected:
	/// Database handler friend class.
	friend class DBHandler;

	/**
	 * Algorithm Queue constructor. This constructor creates a new instance
	 * of AlgQueue using the arguments.
	 * @param grid name of the grid
	 * @param algName name of the algorithm queue
	 * @param maxPackSize maximum package size
	 * @param statStr string representation of processing statistics
	 */
	AlgQueue(const string &grid, const string &algName, const unsigned maxPackSize, const string &statStr);

    private:
	/**
	 * Algorithm Queue constructor. This constructor creates a new instance
	 * of AlgQueue using the arguments.
	 * @param grid name of the grid
	 * @param algName name of the algorithm queue
	 * @param maxPackSize maximum package size
	 */
	AlgQueue(const string &grid, const string &algName, const unsigned maxPackSize);

	/// The grid the algorithm queue belongs to
	string grid;

	/// The name of the algorithm queue
	string tname;

	/// Maximum package size supported by the algorithm
	unsigned mPSize;

	/// Processing statistics
	vector<processStatistics> pStats;
};

#endif  /* ALGQUEUE_H */
