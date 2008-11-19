#ifndef DCAPIHANDLERSINGLE_H
#define DCAPIHANDLERSINGLE_H

#include "Job.h"
#include "GridHandler.h"

#include <vector>

using namespace std;

class DCAPIHandlerSingle: public GridHandler {
public:
	DCAPIHandlerSingle(GKeyFile *config, const char *instance);
	~DCAPIHandlerSingle();
	void submitJobs(JobVector &jobs) throw (BackendException *);
	void updateStatus(void) throw (BackendException *);
	void poll(Job *job) throw (BackendException *) {}

	static GridHandler *getInstance(GKeyFile *config, const char *instance);
};

#endif /* DCAPIHANDLERSINGLE_H */
