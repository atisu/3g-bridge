#ifndef JOB_H
#define JOB_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <map>
#include <string>
#include <vector>
#include "AlgQueue.h"


using namespace std;

enum JobStatus {
	INIT,
	RUNNING,
	FINISHED,
	ERROR,
	CANCEL
};

class Job {
public:
	/* Constructor & destructor */
	Job(const char *id, const char *name, const char *grid, const char *args, JobStatus status);
	~Job() {};

	/* Accessor functions for the immutable fields */
	const string &getId() const { return id; }
	const string &getName() const { return name; }
	const string &getGrid() const { return grid; }
	const string &getArgs() const { return args; }
	AlgQueue *getAlgQueue() { return algQ; }

	/* Input files */
	void addInput(const string &localname, const string &fsyspath);
	vector<string> getInputs() const;
	const string &getInputPath(const string localname) { return inputs[localname]; }

	/* Output files */
	void addOutput(const string &localname, const string &fsyspath);
	vector<string> getOutputs() const;
	const string &getOutputPath(const string localname) { return outputs[localname]; }

	/* Grid ID handling */
	void setGridId(const string &sId);
	const string &getGridId() const { return gridId; }

	/* Status handling */
	void setStatus(JobStatus nStat);
	JobStatus getStatus() const { return status; }

	void deleteJob();
private:
	string id;
	string name;
	string args;
	string grid;
	string gridId;
	AlgQueue *algQ;
	map<string, string> inputs;
	map<string, string> outputs;
	JobStatus status;
};

class JobVector: public vector<Job *> {
public:
	JobVector() { vector<Job *>(); }
	~JobVector() { clear(); }
	void clear();
};

#endif /* JOB_H */
