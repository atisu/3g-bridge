#ifndef __DCAPIHANDLER_H
#define __DCAPIHANDLER_H

#include "CGJob.h"
#include "GridHandler.h"
#include "DBHandler.h"

#include <vector>

using namespace std;

class DCAPIHandler: public GridHandler {
public:
	DCAPIHandler(DBHandler *jobdb, const string conf);
	~DCAPIHandler();
	void submitJobs(vector<CGJob *> *jobs) throw (BackendException &);
	void updateStatus(void) throw (BackendException &);

	/* Obsolote/unimplemented methods */
	void getStatus(vector<CGJob *> *jobs) throw (BackendException &) {};
	void getOutputs(vector<CGJob *> *jobs) throw (BackendException &) {};
	void cancelJobs(vector <CGJob *> *jobs) throw (BackendException &) {};
};

#endif
