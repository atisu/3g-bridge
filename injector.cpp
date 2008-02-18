#include <iostream>
#include <sstream>
#include <fstream>
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
    bool wait = true;
    int jobid = 0;
    
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
	// Wait for job
	ValueArg<int> waitArg("w", "wait", "Set to 0 if you don't want to wait for job to finish", false, 1, "int");

	// add parameters to command-line parse object
	cmd.add(jobNameArg);
	cmd.add(cmdLineArg);
	cmd.add(algNameArg);
	cmd.add(inLocalArg);
	cmd.add(inPathArg);
	cmd.add(outLocalArg);
	cmd.add(waitArg);

	// Parse command-line arguments into ValueArgs
	cmd.parse(argc, argv);
	
	// Store command-line arguments
	jobName  = jobNameArg.getValue();
	cmdLine  = cmdLineArg.getValue();
	algName  = algNameArg.getValue(); 
	inLocal  = inLocalArg.getValue();
	inPath   = inPathArg.getValue();
	outLocal = outLocalArg.getValue();
	int tmp = waitArg.getValue();
	if (tmp >= 1)
	    wait = true;
	else
	    wait = false; 
	
	// explode list into vector<string>
	vector<string> in = explode(',', inLocal);
	vector<string> inp = explode(',', inPath);
	vector<string> out = explode(',', outLocal);
	map<string, string> *inputs = new map<string, string>();
	
	if (in.size() != inp.size()) {
	    cerr << "Input local names and input path parameters must have the same number of elements" << endl;
	    exit(-1);
	}
	
	// make map of input local names and input paths
	for (int i = 0; i < in.size(); i++)
	    inputs->insert(make_pair(in.at(i), inp.at(i)));
	    
	{ // transaction scope
	    Transaction trans(con);
    	    // insert into mysql...
	    cg_job job_row(0, jobName, cmdLine, algName, "CG_INIT");
    	    query.insert(job_row);
	    query.execute();
	    query.reset();
	
    	    // Get row id of new job
	    query << "SELECT * FROM cg_job WHERE name = \"" << jobName << "\""; 
	    vector<cg_job> job;
	    query.storein(job);
	    int id = job.at(0).id;
	    
	    // Put inputs in cg_inputs table
	    for(map<string, string>::iterator it = inputs->begin(); it != inputs->end(); it++) {
		cg_inputs input_row(0, it->first, it->second, id);
		query.insert(input_row);
		query.execute();
		query.reset();
	    }

	    // Put outputs in cg_outputs table
	    for(vector<string>::iterator it = out.begin(); it != out.end(); it++) {
		cg_outputs output_row(0, *it, "", id);
		query.insert(output_row);
		query.execute();
		query.reset();
	    }
	    trans.commit();
	} // end of transaction scope
	
	while (wait) {
	    // wait for job to finish
	    query << "SELECT * FROM cg_job WHERE name = \"" << jobName << "\"";
	    vector<cg_job> job;
	    query.storein(job);
	    string status = job.at(0).status;
	    jobid = job.at(0).id;
	
	    if (status == "CG_FINISHED")  {
		break;
    	    } else 
		sleep(10);
	}
	
	// If we were waiting for the output, print it
	if (wait) {
	    query << "SELECT * FROM cg_outputs WHERE localname != \"stdout.txt\" AND localname != \"stderr.txt\" AND jobid = \"" << jobid << "\"";
	    vector<cg_outputs> outputs;
	    query.storein(outputs);
	    for (vector<cg_outputs>::iterator it = outputs.begin(); it != outputs.end(); it++)
		cout << it->path << endl;
	}
	
    } catch (const BadQuery& er) {
	 cout << "Job with name '" << jobName << "' already exists. Choose another name!" << endl;
	 return -1;
    } catch (exception& ex) {
	cerr << ex.what() << endl;
	return -1;
    }

    return 0;
}
