#include <map>
#include <string>
#include <cstring>

#include "CGJob.h"

using namespace std;

CGJob::CGJob(const string tname, list<string> *arglist, CGAlg &aType):name(tname)
{
    inputs.clear();
    outputs.clear();
    type = &aType;
    argv = arglist;
}

CGJob::~CGJob()
{
}

void CGJob::addInput(const string localname, const string fsyspath)
{
    inputs[localname] = fsyspath;
}

void CGJob::addOutput(const string localname)
{
    outputs[localname] = string("undefined");
}

void CGJob::setOutputPath(const string localname, const string fsyspath)
{
    outputs[localname] = fsyspath;
}

vector<string> CGJob::getInputs() const
{
    map<string, string>::const_iterator it;
    vector<string> rval;
    for (it = inputs.begin(); it != inputs.end(); it++) {
	rval.push_back(it->first);
    }
    return rval;
}

vector<string> CGJob::getOutputs() const
{
    map<string, string>::const_iterator it;
    vector<string> rval;
    for (it = outputs.begin(); it != outputs.end(); it++) {
	rval.push_back(it->first);
    }
    return rval;
}

string CGJob::getInputPath(const string localname) const
{
    map<string, string>::const_iterator it = inputs.find(localname);
    return it->second;
}

string CGJob::getOutputPath(const string localname)
{
    return outputs[localname];
}
