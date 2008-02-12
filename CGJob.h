#ifndef __CG_JOB_H_
#define __CG_JOB_H_

#include <map>
#include <string>
#include <vector>
#include "common.h"
#include "CGAlg.h"

using namespace std;

class CGJob {
private:
    CGAlg *type;
    string name;
    string cmdline;
    string ID;
    char *wuId;
    map<string, string> inputs;
    map<string, string> outputs;
    CGJobStatus status;
public:
    CGJob(const string name, CGAlg &aType);
    ~CGJob();
    void addInput(const string localname, const string fsyspath);
    void addOutput(const string localname);
    void setOutputPath(const string &localname, const string fsyspath);
    string getName() { return name; }
    string getCmdLine() { return cmdline; }
    CGAlg *getType() { return type; }
    vector<string> getInputs() const;
    vector<string> getOutputs() const;
    void setId(const string tID) { ID = tID; }
    string getId() const { return ID; }
    char *getWUId() const { return wuId; }
    void setCmdLine(string cline) { cmdline = cline; }
    void setWUId(char *sId) { wuId = strdup(sId); }
    string getInputPath(const string localname) const;
    string getOutputPath(const string &localname) const;
    CGJobStatus getStatus() const { return status; }
    void setStatus(CGJobStatus nStat) { status = nStat; }
};

#endif
