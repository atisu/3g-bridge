#ifndef __DCAPIHANDLER_H
#define __DCAPIHANDLER_H

#include "CGJob.h"
#include "GridHandler.h"

#include <vector>

using namespace std;

class DCAPIHandler: public GridHandler {
public:
	DCAPIHandler(GKeyFile *config, const char *instance);
	~DCAPIHandler();
	void submitJobs(vector<CGJob *> *jobs) throw (BackendException &);
	void updateStatus(void) throw (BackendException &);

	static GridHandler *getInstance(GKeyFile *config, const char *instance);

	/* Obsolote/unimplemented methods */
	void cancelJobs(vector <CGJob *> *jobs) throw (BackendException &) {};
};

#endif
