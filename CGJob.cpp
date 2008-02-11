#include <iostream>
#include <map>
#include <string>

#include "CGJob.h"

using namespace std;

CGJob::CGJob(const string tname, CGAlg &aType):name(tname)
{
    inputs.clear();
    outputs.clear();
    type = &aType;
    name = tname;
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

void CGJob::setOutputPath(const string &localname, const string fsyspath)
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
    cout << "Entered getInputPath(), localname to find " << localname <<  endl;
    map<string, string>::const_iterator it = inputs.find(localname);
    cout << "Inputpath: " << it->second << endl;
    return it->second;
}

string CGJob::getOutputPath(const string &localname) const
{
    return "undefinedoutput";
}
