#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <sstream>
#include <fstream>
#include <map>
#include <string>
#include <mysql++/mysql++.h>
//#include <mysql++/transaction.h>
#include <mysql++/null.h>
#include <unistd.h>
#include <getopt.h>

#include "CGSqlStruct.h"

extern char *optarg;
extern int optind, opterr, optopt;

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

void usage(const char *cmdname)
{
    cout << "--== Prototype job submitter for Cancergrid ==--" << endl << endl;
    cout << "Usage: " << cmdname << " [switches]" << endl;
    cout << " * -j --jobname  [name]     job's name" << endl;
    cout << "   -c --cmdline  [cmdline]  command line options" << endl;
    cout << " * -a --algname  [algname]  algorithm name" << endl;
    cout << "   -i --inlocal  [inlocal]  comma separated list of input files used" << endl;
    cout << "   -p --inpath   [inpath]   comma separated list of input paths used" << endl;
    cout << "   -o --outlocal [outlocal] comma separated list of output files used" << endl;
    cout << "   -w --wait                use if the tool should wait for the job to finish" << endl;
    cout << "   -h --help                this help screen" << endl;
    cout << " *: switch is mandatory" << endl;
    exit(0);
}

int main(int argc, char **argv)
{
    Connection con;
    string jobName = "";
    string cmdLine = "";
    string algName = "";
    string inLocal = "";
    string inPath = "";
    string outLocal = "";
    bool wait = false;
    int jobid = 0;

    int c;
    while (1) {
        int option_index = 0;
	static struct option long_options[] = {
	    {"jobname", 1, 0, 'j'},
	    {"cmdline", 1, 0, 'c'},
	    {"algname", 1, 0, 'a'},
	    {"inlocal", 1, 0, 'i'},
	    {"inpath", 1, 0, 'p'},
	    {"outlocal", 1, 0, 'o'},
	    {"wait", 1, 0, 'w'},
	    {"help", 0, 0, 'h'},
	    {0, 0, 0, 0}
	};
	c = getopt_long(argc, argv, "j:c:a:i:p:o:w", long_options, &option_index);
	if (c == -1)
	    break;
	switch (c) {
	    case 'j':
		jobName = string(optarg);
		break;
	    case 'c':
		cmdLine = string(optarg);
		break;
	    case 'a':
		algName = string(optarg);
		break;
	    case 'i':
		inLocal = string(optarg);
		break;
	    case 'p':
		inPath = string(optarg);
		break;
	    case 'o':
		outLocal = string(optarg);
		break;
	    case 'w':
		wait = true;
		break;
	    case '?':
		usage(argv[0]);
		break;
	}
    }

    if (jobName == "" || algName == "")
	usage(argv[0]);

    try {
        con.connect("boinc_cancergrid", "0", "boinc-cancergrid", "czowtjhdlo");
	Query query = con.query();
	
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
	for (unsigned i = 0; i < in.size(); i++)
	    inputs->insert(make_pair(in.at(i), inp.at(i)));
	    
//	{ // transaction scope
//	    Transaction trans(con);
    	    // insert into mysql...
	    cg_job job_row(0, jobName, cmdLine, algName, "CG_INIT", "");
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
//	    trans.commit();
//	} // end of transaction scope

	while (wait) {
	    // wait for job to finish
	    query = con.query();

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
	    query = con.query();

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
