#ifndef GRIDHANDLER_H
#define GRIDHANDLER_H

#include "BackendException.h"
#include "Job.h"

#include <glib.h>

#include <vector>

using namespace std;

/*
 * GridHandler interface. Used by the Queue Manager to:
 *  - submit jobs (as WUs)
 *  - update status of jobs (or WUs)
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
    }

    GridHandler(GKeyFile *config, const char *instance);

    virtual ~GridHandler() {}

    const char *getName(void) const { return name.c_str(); }

    bool schGroupByNames() const { return groupByNames; }

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
     * Poll the status of a submitted job. This is used as a callback for
     * DBHandler::pollJobs().
     */
    virtual void poll(Job *job) throw (BackendException &) = 0;

protected:
    bool groupByNames;
    string name;
};

#endif /* GRIDHANDLER_H */
