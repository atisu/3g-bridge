#ifndef __DCAPIHANDLER_H
#define __DCAPIHANDLER_H

#include <vector>
#include "CGJob.h"
#include "GridHandler.h"

using namespace std;

class DCAPIHandler : public GridGHandler {
public:
    DCAPIHandler(const char *conf);
    ~DCAPIHandler();
    void submitJobs(vector<CGJob *> *jobs);
    void getStatus(vector<CGJob *> *jobs);
    void getOutputs(vector<CGJob *> *jobs);
    void cancelJobs(vector <CGJob *> *jobs);
};

#endif
