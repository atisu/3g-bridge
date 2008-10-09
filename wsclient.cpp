#include <getopt.h>
#include <string.h>

#include <vector>
#include <iostream>

#include "soap/soapH.h"
#include "soap/G3BridgeBinding.nsmap"


using namespace std;


void help_screen(bool doshort = false);
void parse_args(ns2__Job &job, const char *args);
void parse_inputs(ns2__Job &job, const char *inputs);
void parse_outputs(ns2__Job &job, const char *outputs);
void handle_add(ns2__Job &job, const char *endpoint);

int main(int argc, char **argv)
{
	char *mode = NULL, *endpoint = NULL;
	ns2__Job job;

	while (1)
	{
		int c, d;
		int option_index = 0;
		static struct option long_options[] =
		{
			{"mode", 1, &c, 'm'},
			{"name", 1, &c, 'n'},
			{"grid", 1, &c, 'g'},
			{"args", 1, &c, 'a'},
			{"ins", 1, &c, 'i'},
			{"outs", 1, &c, 'o'},
			{"endpoint", 1, &c, 'e'},
			{"help", 0, &c, 'h'}
		};
		d = getopt_long(argc, argv, "m:n:g:a:i:o:e:h", long_options, &option_index);
		if (-1 == d)
			break;
		if (d)
			c = d;
		switch (c)
		{
		case 'm':
			mode = strdup(optarg);
			break;
		case 'n':
			job.alg = strdup(optarg);
			break;
		case 'g':
			job.grid = strdup(optarg);
			break;
		case 'a':
			parse_args(job, optarg);
			break;
		case 'i':
			parse_inputs(job, optarg);
			break;
		case 'o':
			parse_outputs(job, optarg);
			break;
		case 'e':
			endpoint = strdup(optarg);
			break;
		case 'h':
		default:
			help_screen();
			exit(0);
			break;
		}
	}

	if (!mode)
	{
		cerr << "Error: no mode selected!" << endl;
		help_screen(true);
	}

	if (!endpoint)
	{
		cerr << "Error: no endpoint URL specified!" << endl;
		help_screen(true);
	}

	if (!strcmp(mode, "add"))
		handle_add(job, endpoint);

	return(0);
}


void help_screen(bool doshort)
{
	if (doshort)
	{
		cerr << "Use -h or --help for usage instructions." << endl;
		exit(-1);
	}
	cout << endl;
	cout << "Command line switches to wsclient:" << endl;
	cout << "    -m|--mode [add|status|del|output]  Operation mode (*)" << endl;
	cout << "    -e|--endpoint [URL]                Endpoint of service (*)" << endl;
	cout << "    -h|--help                          Print this help" << endl;
	cout << "  'add' mode switches:" << endl;
	cout << "    -n|--name [algorithm name]         Name of the algorithm (*)" << endl;
	cout << "    -g|--grid [grid name]              Destination grid (*)" << endl;
	cout << "    -a|--args [arguments]              Comma-separated list of args" << endl;
	cout << "    -i|--ins  [in1=URL1,...]           List of input files" << endl;
	cout << "    -o|--outs [out1,...]               List of output files" << endl;
	cout << "  'status', 'del' and 'output' mode switches:" << endl;
	cout << "    -j|--jid [Job identifier]          Job's identifier to use (*)" << endl;
	cout << endl;
	cout << "  *: argument is mandatory" << endl;
}


void split_by_delims(const char *src, const char *delims, vector<string> &res)
{
	char *tsrc = strdup(src);
	char *str1 = strtok(tsrc, delims);
	while (str1)
	{
		res.push_back(string(str1));
		str1 = strtok(NULL, delims);
	}
	free(tsrc);
}


void parse_args(ns2__Job &job, const char *args)
{
	split_by_delims(args, ",", job.args);
}


void parse_inputs(ns2__Job &job, const char *inputs)
{
	vector<string> ins;
	split_by_delims(inputs, ",", ins);
	for (unsigned i = 0; i < ins.size(); i++)
	{
		vector<string> names;
		split_by_delims(ins.at(i).c_str(), "=", names);
		if (names.size() % 2 || !names.size())
		{
			cerr << "Malformed input definition string: " << ins.at(i) << endl;
			help_screen(true);
		}
		ns2__LogicalFile *lf = new ns2__LogicalFile;
		lf->logicalName = names.at(0);
		lf->URL = names.at(1);
		job.inputs.push_back(lf);
	}
}


void parse_outputs(ns2__Job &job, const char *outputs)
{
	split_by_delims(outputs, ",", job.outputs);
}


void handle_add(ns2__Job &job, const char *endpoint)
{
	if ("" == job.alg || "" == job.grid)
	{
		cerr << "Job addition problem: either name or grid undefined!" << endl;
		help_screen(true);
	}

	struct ns1__submitResponse IDs;
	ns2__JobList jList;
	jList.job.push_back(&job);

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call_ns1__submit(soap, endpoint, NULL, &jList, IDs))
	{
		soap_print_fault(soap, stderr);
		exit(-1);
	}
	cout << "The job ID is: " << IDs.jobids->jobid.at(0) << endl;
	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);
}
