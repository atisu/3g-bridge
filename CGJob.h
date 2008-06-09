#ifndef __CG_JOB_H_
#define __CG_JOB_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <map>
#include <string>
#include <vector>
#include "common.h"
#include "CGAlgQueue.h"


using namespace std;


class CGJob {
public:
	/* Constructor & destructor */
	CGJob(const char *id, const char *name, const char *args, CGAlgQueue *algQ):
			id(id),name(name),args(args),algQ(algQ) {};
	~CGJob() {};

	/* Accessor functions for the immutable fields */
	const string &getId() const { return id; }
	const string &getName() const { return name; }
	const string &getGrid() const { return grid; }
	const string &getArgs() const { return args; }
	CGAlgQueue *getAlgQueue() { return algQ; }

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
	void setStatus(CGJobStatus nStat);
	CGJobStatus getStatus() const { return status; }

	void deleteJob();
private:
	string id;
	string name;
	string args;
	string grid;
	string gridId;
	CGAlgQueue *algQ;
	map<string, string> inputs;
	map<string, string> outputs;
	CGJobStatus status;
};

class JobVector: public vector<CGJob *> {
public:
	JobVector() { vector<CGJob *>(); }
	~JobVector() { clear(); }
	void clear();
};

#endif
