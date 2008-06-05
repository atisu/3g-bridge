#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <map>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include "Logging.h"
#include "DBHandler.h"
#include "QMException.h"

#include <getopt.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <glib.h>

extern char *optarg;
extern int optind, opterr, optopt;

GKeyFile *global_config = NULL;

using namespace std;

void usage(const char *cmdname)
{
    cout << "--== Prototype job submitter for Cancergrid ==--" << endl << endl;
    cout << "Usage: " << cmdname << " config [switches]" << endl;
    cout << " * -a, --algname <NAME>          algorithm (executable) name" << endl;
    cout << "   -c, --cmdline <ARGUMENTS>     command-line arguments" << endl;
    cout << " * -i, --input <LOGICAL>:<PATH>  logical/physical input file name" << endl;
    cout << " * -o, --output <LOGICAL>:<PATH> logical/physical output file name" << endl;
    cout << "   -h, --help                    this help screen" << endl;
    cout << " *: switch is mandatory" << endl;
    exit(0);
}

int main(int argc, char **argv)
{
    string cmdLine = "";
    string algName = "";
    vector<string *> inputs;
    vector<string *> outputs;
    GError *error;

    int c;
    while (1) {
        int option_index = 0;
	static struct option long_options[] = {
	    {"algname", 1, 0, 'a'},
	    {"cmdline", 1, 0, 'c'},
	    {"input", 1, 0, 'i'},
	    {"output", 1, 0, 'o'},
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

    global_config = g_key_file_new();
    error = NULL;
    g_key_file_load_from_file(global_config, argv[1], G_KEY_FILE_NONE, &error);
    if (error)
    {
	    cerr << "Failed to load the config file: " << error->message << endl;
	    exit(1);
    }

    DBHandler *dbh;
    try {
	dbh = DBHandler::get();
    }
    catch (QMException &e) {
        cerr << "Error: " << e.what() << endl;
	exit(1);
    }

    // Check for mandatory options
    if (algName == "" || !inputs.size() || !outputs.size())
	usage(argv[0]);

    // Generate an identifier for our job
    uuid_t jid;
    char sid[37];
    uuid_generate(jid);
    uuid_unparse(jid, sid);

    CGJob job(algName, cmdLine, 0);
    job.setId(sid);

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
	job.addInput(logical, path);
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
	job.addOutput(logical, path);
    }

    try {
	dbh->addJob(job);
    } catch (QMException &e) {
	cerr << "Error: " << e.what() << endl;
	return -1;
    }

    return 0;
}
