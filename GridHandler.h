#ifndef __GRIDHANDLER_H
#define __GRIDHANDLER_H

#include "BackendException.h"
#include "Job.h"

#include <glib.h>

#include <vector>

using namespace std;

/*
 * GridHandler interface. Used by the Queue Manager to:
 *  - submit jobs (as WUs)
 *  - query status of jobs (or WUs)
 *  - cancel jobs (or WUs)
 *  - get output
 */
class GridHandler {
public:
    /**
     * Initialize GridHandler. For DC-API this should call DC_initMaster, for
     * EGEE should create the config context, ...
     */
    GridHandler()
    {
	groupByNames = false;
	maxGroupSize = 0;
    }

    GridHandler(GKeyFile *config, const char *instance);

    virtual ~GridHandler() {}

    const char *getName(void) const { return name.c_str(); }

    bool schGroupByNames() const { return groupByNames; }
    unsigned schMaxGroupSize() const { return maxGroupSize; }

    /**
     * Submit jobs in the argument. Set the different properties of jobs (Grid
     * ID, status, ...) through the objects' property change functions.
     */
    virtual void submitJobs(JobVector &jobs) throw (BackendException &) = 0;

    /**
     * Update the status of previously submitted jobs in the database.
     */
    virtual void updateStatus(void) throw (BackendException &) = 0;

    /**
     * Cancel jobs.
     */
    virtual void cancelJobs(JobVector &jobs) throw (BackendException &) = 0;

protected:
    bool groupByNames;
    unsigned maxGroupSize;
    string name;
};

#endif
