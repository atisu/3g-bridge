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
void handle_status(const char *jobid, const char *endpoint);
void handle_del(const char *jobid, const char *endpoint);
void handle_output(const char *jobid, const char *endpoint);


int main(int argc, char **argv)
{
	char *mode = NULL, *endpoint = NULL, *jobid = NULL;
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
			{"jobid", 1, &c, 'j'},
			{"endpoint", 1, &c, 'e'},
			{"help", 0, &c, 'h'}
		};
		d = getopt_long(argc, argv, "m:n:g:a:i:o:j:e:h", long_options, &option_index);
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
		case 'j':
			jobid = strdup(optarg);
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
	else if (!strcmp(mode, "status"))
		handle_status(jobid, endpoint);
	else if (!strcmp(mode, "del"))
		handle_status(jobid, endpoint);
	else if (!strcmp(mode, "output"))
		handle_status(jobid, endpoint);
	else
	{
		cerr << "Error: invalid mode specified: \"" << mode << "\"!" << endl;
		help_screen(true);
	}

	return(0);
}


/**
 * Print usage instructions
 * @param doshort print how to invoke help when true and exit, print full usage
 *                otherwise
 */
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


/**
 * Split string.
 * @param src string to split
 * @param delims delimiters to use
 * @param[out] res vector to store splitted values
 */
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


/**
 * Parse command line arguments
 * @param[out] job ns2__Job object to fill in
 * @param args argument string to parse
 */
void parse_args(ns2__Job &job, const char *args)
{
	split_by_delims(args, ",", job.args);
}


/**
 * Parse input files
 * @param[out] job ns2__Job object to fill in
 * @param inputs inputs string to parse
 */
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


/**
 * Parse output files
 * @param[out] job ns2__Job object to fill in
 * @param outputs outputs string to parse
 */
void parse_outputs(ns2__Job &job, const char *outputs)
{
	split_by_delims(outputs, ",", job.outputs);
}


/**
 * Handle submission
 * @param job ns2__Job object to send to the Service
 * @param endpoint Service endpoint to use
 */
void handle_add(ns2__Job &job, const char *endpoint)
{
	if ("" == job.alg || "" == job.grid)
	{
		cerr << "Job addition problem: either name or grid undefined!" << endl;
		help_screen(true);
	}

	struct ns1__submitResponse IDs;
	ns2__JobList jList;
	jList.job.clear();
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


/**
 * Check if job identifier is not empty. Exits when job identifier is empty
 * @param val string to check
 */
void check_jobid(const char *val)
{
	if (val && strlen(val))
		return;
		
	cerr << "Error: job identifier isn't set!" << endl;
	help_screen(true);
}


/**
 * Handle status query
 * @param jobid job identifier we would like to use
 * @param endpoint Service endpoint to use
 */
void handle_status(const char *jobid, const char *endpoint)
{
	ns2__JobIDList jList;
	struct ns1__getStatusResponse resp;
	struct {
		ns2__JobStatus st;
		string str;
	} statToStr[] = {
		{ns2__JobStatus__UNKNOWN,  "Unknown" },
		{ns2__JobStatus__INIT,     "Init"    },
		{ns2__JobStatus__RUNNING,  "Running" },
		{ns2__JobStatus__FINISHED, "Finished"},
		{ns2__JobStatus__ERROR,    "Error"   }
	};

	check_jobid(jobid);
	jList.jobid.clear();
	jList.jobid.push_back(string(jobid));

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call_ns1__getStatus(soap, endpoint, NULL, &jList, resp))
	{
		soap_print_fault(soap, stderr);
		exit(-1);
	}
	ns2__JobStatus st = resp.statuses->status.at(0);
	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);

	cout << "Status of job \"" << jobid << "\" is: " << statToStr[st].str << endl;
}


/**
 * Handle job delete
 * @param jobid job identifier we would like to use
 * @param endpoint Service endpoint to use
 */
void handle_del(const char *jobid, const char *endpoint)
{
	ns2__JobIDList jList;
	struct ns1__delJobResponse resp;

	check_jobid(jobid);
	jList.jobid.clear();
	jList.jobid.push_back(string(jobid));

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call_ns1__delJob(soap, endpoint, NULL, &jList, resp))
	{
		soap_print_fault(soap, stderr);
		exit(-1);
	}
	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);
}


/**
 * Handle job output query
 * @param jobid job identifier we would like to use
 * @param endpoint Service endpoint to use
 */
void handle_output(const char *jobid, const char *endpoint)
{
	ns2__JobIDList jList;
	struct ns1__getOutputResponse resp;

	check_jobid(jobid);
	jList.jobid.clear();
	jList.jobid.push_back(string(jobid));

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call_ns1__getOutput(soap, endpoint, NULL, &jList, resp))
	{
		soap_print_fault(soap, stderr);
		exit(-1);
	}

	ns2__JobOutput *jout = resp.outputs->output.at(0);
	cout << "Output files for job \"" << jout->jobid << "\":" << endl;
	for (unsigned i = 0; i < jout->output.size(); i++)
	{
		ns2__LogicalFile *lf = jout->output.at(i);
		cout << "    " << lf->logicalName << " - " << lf->URL << endl;
	}

	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);
}
