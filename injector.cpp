#include <iostream>
#include <sstream>
#include <map>
#include <string>
#include <mysql++.h>
#include <transaction.h>
#include <null.h>
#include <custom.h>
#include <tclap/CmdLine.h>

#include "CGSqlStruct.h"

using namespace TCLAP;
using namespace mysqlpp;
using namespace std;

static vector<string> explode(const char sep, string src)
{
    vector<string> output;
    string token;
    istringstream iss(src);
    
    while (getline(iss, token, sep)) {
	output.push_back(token);
    }
    return output;
}

int main(int argc, char **argv)
{
    Connection con;
    CmdLine cmd("--== Prototype job submitter for Cancergrid ==--", ' ', "1.0");
    string jobName = "";
    string cmdLine = "";
    string algName = "";
    string inLocal = "";
    string inPath = "";
    string outLocal = "";
    
    try {
        con.connect("boinc_szdgr", "0", "boinc-szdgr", "VfxVqw0PHT");
	Query query = con.query();
	
	// Jobname
	ValueArg<string> jobNameArg("j", "jobname", "Name of job", true, "default", "string");
	// Command-line params
	ValueArg<string> cmdLineArg("c", "cmdline", "Command-line arguments to be passed to the job", true, "default", "string");
	// Algorithm Name
	ValueArg<string> algNameArg("a", "algname", "Name of algorithm", true, "default", "string");
	// Comma separated local name of inputs
	ValueArg<string> inLocalArg("i", "inlocal", "Comma separated list of local names of input files", true, "default", "string");
	// Comma separated path of inputs
	ValueArg<string> inPathArg("p", "inpath", "Comma separated list of path of input files", true, "default", "string");
	// Comma separated local name of inputs
	ValueArg<string> outLocalArg("o", "outlocal", "Comma separated list of local names of output files", true, "default", "string");

	// add parameters to command-line parse object
	cmd.add(jobNameArg);
	cmd.add(cmdLineArg);
	cmd.add(algNameArg);
	cmd.add(inLocalArg);
	cmd.add(inPathArg);
	cmd.add(outLocalArg);

	// Parse command-line arguments into ValueArgs
	cmd.parse(argc, argv);
	
	// Store command-line arguments
	jobName  = jobNameArg.getValue();
	cmdLine  = cmdLineArg.getValue();
	algName  = algNameArg.getValue(); 
	inLocal  = inLocalArg.getValue();
	inPath   = inPathArg.getValue();
	outLocal = outLocalArg.getValue();
	
	// explode list into vector<string>
	vector<string> in = explode(',', inLocal);
	vector<string> inp = explode(',', inPath);
	vector<string> out = explode(',', outLocal);
	map<string, string> *inputs = new map<string, string>();
	
	if (in.size() != inp.size()) {
	    cerr << "Inlocal and Inpath parameters must have the same number of elements" << endl;
	    exit(-1);
	}
	// make map of input local names and input paths
	for (int i = 0; i < in.size(); i++)
	    inputs->insert(make_pair(in.at(i), inp.at(i)));
	    
	{ // transaction scope
	    Transaction trans(con);
    	    // insert into mysql...
	    cg_job job_row(0, jobName, cmdLine, algName);
    	    query.insert(job_row);
	    query.execute();
	    query.reset();
	
    	    // get row id of new job
	    query << "SELECT * FROM cg_job WHERE name = \"" << jobName << "\""; 
	    vector<cg_job> job;
	    query.storein(job);
	    int id = job.at(0).id;
	    
	    for(map<string, string>::iterator it = inputs->begin(); it != inputs->end(); it++) {
		cg_inputs input_row(0, it->first, it->second, id);
		query.insert(input_row);
		query.execute();
		query.reset();
	    }
	    
	    for(vector<string>::iterator it = out.begin(); it != out.end(); it++) {
		cg_outputs output_row(0, *it, "", id);
		query.insert(output_row);
		query.execute();
		query.reset();
	    }
	    trans.commit();
	} // end of transaction scope
    } catch (const BadQuery& er) {
	 cout << "Job with name '" << jobName << "' already exists. Choose another name!" << endl;
	 return -1;
    } catch (exception& ex) {
	cerr << ex.what() << endl;
	return -1;
    }

    return 0;
}
