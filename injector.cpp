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
    cout << " * -a, --algname <NAME>          algorithm (executable) name" << endl;
    cout << "   -c, --cmdline <ARGUMENTS>     command-line arguments" << endl;
    cout << " * -i, --input <LOGICAL>:<PATH>  logical/physical input file name" << endl;
    cout << " * -o, --output <LOGICAL>:<PATH> logical/physical output file name" << endl;
    cout << "   -t, --type <DEST>             destination type (DCAPI, EGEE)" << endl;
    cout << "   -w, --wait                    use if the tool should wait for the job to finish" << endl;
    cout << "   -h, --help                    this help screen" << endl;
    cout << " *: switch is mandatory" << endl;
    exit(0);
}

int main(int argc, char **argv)
{
    Connection con;
    string cmdLine = "";
    string algName = "";
    vector<string *> inputs;
    vector<string *> outputs;
    string type = "";
    bool wait = false;

    int c;
    while (1) {
        int option_index = 0;
	static struct option long_options[] = {
	    {"algname", 1, 0, 'a'},
	    {"cmdline", 1, 0, 'c'},
	    {"input", 1, 0, 'i'},
	    {"output", 1, 0, 'o'},
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
		inputs.push_back(new string(optarg));
		break;
	    case 'o':
		outputs.push_back(new string(optarg));
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

    if (optind >= argc)
    {
	cerr << "Configuration file must be specified" << endl;
	exit(1);
    }

    string dbname, host, user, passwd;

    try {
	QMConfig config(argv[optind]);

	dbname = config.getStr("DB_NAME");
	host = config.getStr("DB_HOST");
	user = config.getStr("DB_USER");
	passwd = config.getStr("DB_PASSWORD");
    }
    catch (string &s) {
        cerr << "Error: " << s << endl;
	exit(1);
    }

    // Check for mandatory options
    if (algName == "" || !inputs.size() || !outputs.size())
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
	
//	{ // transaction scope
//	    Transaction trans(con);
    	    // insert into mysql...
	    query << "INSERT INTO cg_job(id, alg, status, args) VALUES(\"" << sid << "\",\"" << algName << "\",\"INIT\",\"" << cmdLine << "\")";
	    query.execute();

	    for (vector<string *>::const_iterator it = inputs.begin(); it != inputs.end(); it++)
	    {
		size_t pos = (*it)->find_first_of(':');
		if (pos == string::npos)
		{
		    cerr << "Invalid input specification: " << *it << endl;
		    exit(1);
		}
		string logical = (*it)->substr(0, pos);
		string path = (*it)->substr(pos + 1);
		query.reset();
		query << "INSERT INTO cg_inputs (id, localname, path) VALUES(\"" << sid << "\",\"" << logical << "\",\"" << path << "\");";
		query.execute();
	    }

	    for (vector<string *>::const_iterator it = outputs.begin(); it != outputs.end(); it++)
	    {
		size_t pos = (*it)->find_first_of(':');
		if (pos == string::npos)
		{
		    cerr << "Invalid output specification: " << *it << endl;
		    exit(1);
		}
		string logical = (*it)->substr(0, pos);
		string path = (*it)->substr(pos + 1);
		query.reset();
		query << "INSERT INTO cg_outputs (id, localname, path) VALUES(\"" << sid << "\",\"" << logical << "\",\"" << path << "\");";
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
