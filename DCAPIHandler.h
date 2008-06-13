#ifndef DCAPIHANDLER_H
#define DCAPIHANDLER_H

#include "Job.h"
#include "GridHandler.h"

#include <vector>

using namespace std;

class DCAPIHandler: public GridHandler {
public:
	DCAPIHandler(GKeyFile *config, const char *instance);
	~DCAPIHandler();
	void submitJobs(JobVector &jobs) throw (BackendException &);
	void updateStatus(void) throw (BackendException &);
	void poll(Job *job) throw (BackendException &);

	static GridHandler *getInstance(GKeyFile *config, const char *instance);
};

#endif /* DCAPIHANDLER_H */
