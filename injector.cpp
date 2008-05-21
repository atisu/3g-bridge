#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <map>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include "Logging.h"
#include "QMConfig.h"

#include <getopt.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <mysql++/null.h>
#include <mysql++/mysql++.h>

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
    cout << "Usage: " << cmdname << " config [switches]" << endl;
    cout << " * -a --algname  [algname]  algorithm (executable) name" << endl;
    cout << "   -c --cmdline  [cmdline]  command line options" << endl;
    cout << " * -i --inlocal  [inlocal]  comma separated list of input files used" << endl;
    cout << " * -p --inpath   [inpath]   comma separated list of input paths used" << endl;
    cout << "   -o --outlocal [outlocal] comma separated list of output files used" << endl;
    cout << "   -t --type     [type]     destination type (DCAPI, EGEE)" << endl;
    cout << "   -w --wait                use if the tool should wait for the job to finish" << endl;
    cout << "   -h --help                this help screen" << endl;
    cout << " *: switch is mandatory" << endl;
    exit(0);
}

int main(int argc, char **argv)
{
    Connection con;
    string cmdLine = "";
    string algName = "";
    string inLocal = "";
    string inPath = "";
    string outLocal = "";
    string type = "";
    bool wait = false;

    int c;
    while (1) {
        int option_index = 0;
	static struct option long_options[] = {
	    {"algname", 1, 0, 'a'},
	    {"cmdline", 1, 0, 'c'},
	    {"inlocal", 1, 0, 'i'},
	    {"inpath", 1, 0, 'p'},
	    {"outlocal", 1, 0, 'o'},
	    {"type", 1, 0, 't'},
	    {"wait", 1, 0, 'w'},
	    {"help", 0, 0, 'h'},
	    {0, 0, 0, 0}
	};
	c = getopt_long(argc, argv, "c:a:i:p:o:t:w", long_options, &option_index);
	if (c == -1)
	    break;
	switch (c) {
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
	    case 't':
		type = string(optarg);
		break;
	    case 'w':
		wait = true;
		break;
	    case '?':
		usage(argv[0]);
		break;
	}
    }

    Logging::init(cout, LOG_DEBUG);
    QMConfig config(argv[optind]);
    string dbname = config.getStr("DB_NAME");
    string host = config.getStr("DB_HOST");
    string user = config.getStr("DB_USER");
    string passwd = config.getStr("DB_PASSWORD");

    // Check for mandatory options
    if (algName == "" || inLocal == "" || inPath == "")
	usage(argv[0]);

    // If no type is specified, use DCAPI
    if (type == "")
	type = "DCAPI";

    // Generate an identifier for our job
    uuid_t jid;
    char sid[37];
    uuid_generate(jid);
    uuid_unparse(jid, sid);

    try {
        con.connect(dbname.c_str(), host.c_str(), user.c_str(), passwd.c_str());
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
	    query << "INSERT INTO cg_job(id, alg, status, args) VALUES(\"" << sid << "\",\"" << algName << "\",\"INIT\",\"" << cmdLine << "\")";
	    query.execute();

	    // Put inputs in cg_inputs table
	    for(map<string, string>::iterator it = inputs->begin(); it != inputs->end(); it++) {
		query.reset();
		query << "INSERT INTO cg_inputs VALUES(\"" << sid << "\",\"" << it->first << "\",\"" << it->second << "\");";
		query.execute();
	    }

	    // Put outputs in cg_outputs table
	    for(vector<string>::iterator it = out.begin(); it != out.end(); it++) {
		query.reset();
		query << "INSERT INTO cg_outputs(id,localname) VALUES(\"" << sid << "\",\"" << *it << "\");";
		query.execute();
	    }
//	    trans.commit();
//	} // end of transaction scope

	while (wait) {
	    // wait for job to finish
	    query = con.query();

	    query << "SELECT * FROM cg_job WHERE id = \"" << sid << "\"";
	    vector<Row> job;
	    query.storein(job);
	    string status = string(job[0]["status"]);
	    
	    if (status == "FINISHED")  {
		break;
    	    } else 
		sleep(10);
	}
	
	// If we were waiting for the output, print it
	if (wait) {
	    query = con.query();

	    query << "SELECT * FROM cg_outputs WHERE localname != \"stdout.txt\" AND localname != \"stderr.txt\" AND id = \"" << sid << "\"";
	    vector<Row> outputs;
	    query.storein(outputs);
	    
	    for (vector<Row>::iterator it = outputs.begin(); it != outputs.end(); it++)
		cout << (*it)["path"] << endl;
	}
	
    } catch (const BadQuery& er) {
	 cout << "Job with id '" << sid << "' already exists. Choose another name!" << endl;
	 return -1;
    } catch (exception& ex) {
	cerr << ex.what() << endl;
	return -1;
    }

    return 0;
}
