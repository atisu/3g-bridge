#ifndef __CG_JOB_H_
#define __CG_JOB_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <map>
#include <string>
#include <vector>
#include <list>
#include "common.h"
#include "CGAlg.h"

using namespace std;

class CGJob {
private:
    CGAlg *type;
    string id;
    string name;
    string args;
    list<string> *envs;
    string gridId;
    string property;
    map<string, string> inputs;
    map<string, string> outputs;
    CGJobStatus status;
    CGAlgType dstType;
    string dstLoc;
public:
    CGJob(const string name, list<string> *arglist, CGAlg &aType);
    ~CGJob();
    void addInput(const string localname, const string fsyspath);
    void addOutput(const string localname);
    void setOutputPath(const string localname, const string fsyspath);
    string getName() { return name; }
    string getArgs() { return args; }
    void setEnv(list<string> *envvals) { envs = envvals; }
    list<string> *getEnvs() { return envs; }
    CGAlg *getType() { return type; }
    vector<string> getInputs() const;
    vector<string> getOutputs() const;
    string getGridId() const { return gridId; }
    void setGridId(const string sId) { gridId = sId; }
    string getId() const { return id; }
    void setId(const string sId) { id = sId; }
    string getProperty() const { return property; }
    void setProperty(const string sProp) { property = sProp; }
    CGAlgType getDstType() const { return dstType; }
    void setDstType(const CGAlgType dType) { dstType = dType; }
    string getDstLoc() const { return dstLoc; }
    void setDstLoc(const string dLoc) { dstLoc = dLoc; }
    string getInputPath(const string localname) const;
    string getOutputPath(const string localname);
    CGJobStatus getStatus() const { return status; }
    void setStatus(CGJobStatus nStat) { status = nStat; }
};

#endif
