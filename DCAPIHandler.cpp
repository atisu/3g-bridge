#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <vector>
#include "CGJob.h"
#include "DCAPIHandler.h"

using namespace std;

DCAPIHandler::DCAPIHandler(const char *conf)
{
    if (DC_OK != DC_initMaster(conf))
        throw DC_initMasterError;
}

DCAPIHandler::~DCAPIHandler()
{
}


void DCAPIHandler::submitJobs(vector<CGJob *> *jobs)
{
}

void DCAPIHandler::getStatus(vector<CGJob *> *jobs)
{
}

void DCAPIHandler::getOutputs(vector<CGJob *> *jobs)
{
}

void DCAPIHandler::cancelJobs(vector <CGJob *> *jobs)
{
}
