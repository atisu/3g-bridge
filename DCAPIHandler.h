#ifndef __DCAPIHANDLER_H
#define __DCAPIHANDLER_H

#include "CGJob.h"
#include "GridHandler.h"
#include "DBHandler.h"
#include "QMConfig.h"

#include <vector>

using namespace std;

class DCAPIHandler: public GridHandler {
public:
	DCAPIHandler(DBHandler *jobdb, QMConfig &config);
	~DCAPIHandler();
	void submitJobs(vector<CGJob *> *jobs) throw (BackendException &);
	void updateStatus(void) throw (BackendException &);

	/* Obsolote/unimplemented methods */
	void cancelJobs(vector <CGJob *> *jobs) throw (BackendException &) {};
};

#endif
