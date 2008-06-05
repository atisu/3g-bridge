#ifndef __CG_JOB_H_
#define __CG_JOB_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <map>
#include <list>
#include <string>
#include <vector>
#include "common.h"
#include "CGAlgQueue.h"


using namespace std;


class CGJob {
private:
    string id;
    string name;
    CGAlgQueue *talgQ;
    string targs;
    list<string> *envs;
    string gridId;
    map<string, string> inputs;
    map<string, string> outputs;
    CGJobStatus status;
    string grid;
public:
    CGJob(const string name, string args, CGAlgQueue *algQ);
    ~CGJob();
    void addInput(const string localname, const string fsyspath);
    void addOutput(const string localname, const string fsyspath);
    string getName() { return name; }
    string getArgs() { return targs; }
    void setEnv(list<string> *envvals) { envs = envvals; }
    list<string> *getEnvs() { return envs; }
    CGAlgQueue *getAlgQueue() { return talgQ; }
    vector<string> getInputs() const;
    vector<string> getOutputs() const;
    string getGridId() const { return gridId; }
    void setGridId(const string &sId);
    string getId() const { return id; }
    void setId(const string sId) { id = sId; }
    string getInputPath(const string localname);
    string getOutputPath(const string localname);
    CGJobStatus getStatus() const { return status; }
    void setStatus(CGJobStatus nStat);
    void deleteJob();
};

#endif
