#ifndef __GRIDHANDLER_H
#define __GRIDHANDLER_H

#include <vector>
#include "CGJob.h"

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
    GridHandler() {}
    virtual ~GridHandler() {}
    /**
     * Submit jobs in the argument. Set the different properties of jobs (Grid
     * ID, status, ...) through the objects' property change functions.
     */
    virtual void submitJobs(vector<CGJob *> *jobs) = 0;
    /**
     * Update status of jobs through the objects.
     */
    virtual void getStatus(vector<CGJob *> *jobs) = 0;
    /**
     * Get results.
     */
    virtual void getOutputs(vector<CGJob *> *jobs) = 0;
};

#endif
