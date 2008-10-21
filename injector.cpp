#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <map>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include "DBHandler.h"
#include "QMException.h"

#include <uuid/uuid.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>

extern char *optarg;
extern int optind, opterr, optopt;

static GKeyFile *global_config = NULL;

using namespace std;

void usage(ostream &stream, const char *cmdname, int exitCode)
{
    stream << "Usage: " << cmdname << " <options>" << endl;
    stream << " * -c, --config <FILE>           configuration file name" << endl;
    stream << " * -a, --algname <NAME>          algorithm (executable) name" << endl;
    stream << " * -g, --grid <NAME>             destination grid name" << endl;
    stream << " * -i, --input <LOGICAL>:<PATH>  logical/physical input file name" << endl;
    stream << " * -o, --output <LOGICAL>:<PATH> logical/physical output file name" << endl;
    stream << "   -p, --params <PARAMS>         command-line parameters" << endl;
    stream << "   -h, --help                    this help screen" << endl;
    stream << " *: switch is mandatory" << endl;
    exit(exitCode);
}

int main(int argc, char **argv)
{
    char *cmdLine = NULL;
    char *algName = NULL;
    char *configFile = NULL;
    char *grid = NULL;
    vector<string *> inputs;
    vector<string *> outputs;
    GError *error;

    int c;
    while (1) {
	static struct option long_options[] = {
	    {"algname", 1, 0, 'a'},
	    {"config", 1, 0, 'c'},
	    {"grid", 1, 0, 'g'},
	    {"help", 0, 0, 'h'},
	    {"input", 1, 0, 'i'},
	    {"output", 1, 0, 'o'},
	    {"params", 1, 0, 'p'},
	    {0, 0, 0, 0}
	};
	c = getopt_long(argc, argv, "a:c:g:hi:o:p:", long_options, NULL);
	if (c == -1)
	    break;
	switch (c) {
	    case 'a':
		algName = optarg;
		break;
	    case 'c':
		configFile = optarg;
		break;
	    case 'g':
		grid = optarg;
		break;
	    case 'h':
		usage(cout, argv[0], 0);
		break;
	    case 'i':
		inputs.push_back(new string(optarg));
		break;
	    case 'o':
		outputs.push_back(new string(optarg));
		break;
	    case 'p':
		cmdLine = optarg;
		break;
	    case '?':
		usage(cerr, argv[0], 0);
		break;
	}
    }

    if (argc > optind)
    {
	    cerr << "Garbage on the command line" << endl << endl;
	    usage(cerr, argv[0], 1);
    }

    global_config = g_key_file_new();
    error = NULL;
    g_key_file_load_from_file(global_config, configFile, G_KEY_FILE_NONE, &error);
    if (error)
    {
	    cerr << "Failed to load the config file: " << error->message << endl;
	    exit(1);
    }

    // Check for mandatory options
    if (!configFile)
    {
	cerr << "The config file name is missing" << endl << endl;
	usage(cerr, argv[0], 1);
    }
    if (!algName)
    {
	cerr << "The algorithm name is missing" << endl << endl;
	usage(cerr, argv[0], 1);
    }
    if (!grid)
    {
	cerr << "The destination grid name is missing" << endl << endl;
	usage(cerr, argv[0], 1);
    }
    if (!inputs.size())
    {
	cerr << "There are no input files specified" << endl << endl;
	usage(cerr, argv[0], 1);
    }
    if (!outputs.size())
    {
	cerr << "There are no output files specified" << endl << endl;
	usage(cerr, argv[0], 1);
    }

    // Generate an identifier for our job
    uuid_t jid;
    char sid[37];
    uuid_generate(jid);
    uuid_unparse(jid, sid);

    DBHandler::init(global_config);

    Job job(sid, algName, grid, cmdLine, Job::INIT);

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
	DBHandler *dbh = DBHandler::get();
	dbh->addJob(job);
	DBHandler::put(dbh);
    }
    catch (QMException *e) {
	cerr << "Error: " << e->what() << endl;
	exit(1);
    }

    return 0;
}
