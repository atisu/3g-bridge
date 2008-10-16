#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <getopt.h>
#include <string.h>

#include <vector>
#include <fstream>
#include <iostream>

#include "soap/soapH.h"
#include "soap/G3BridgeBinding.nsmap"


using namespace std;


void help_screen(bool doshort = false);
void parse_inputs(G3BridgeType__Job &job, const char *inputs);
void parse_outputs(G3BridgeType__Job &job, const char *outputs);
void handle_add(G3BridgeType__Job &job, const char *endpoint);
void handle_status(const char *jobid, const char *jidfname, const char *endpoint);
void handle_del(const char *jobid, const char *endpoint);
void handle_output(const char *jobid, const char *endpoint);


int main(int argc, char **argv)
{
	char *mode = NULL, *endpoint = NULL, *jobid = NULL, *jidfname = NULL;
	G3BridgeType__Job job;

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
			{"jidfile", 1, &c, 'f'},
			{"endpoint", 1, &c, 'e'},
			{"help", 0, &c, 'h'}
		};
		d = getopt_long(argc, argv, "m:n:g:a:i:o:j:f:e:h", long_options, &option_index);
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
			job.alg = optarg;
			break;
		case 'g':
			job.grid = optarg;
			break;
		case 'a':
			job.args = optarg;
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
		case 'f':
			jidfname = strdup(optarg);
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
		handle_status(jobid, jidfname, endpoint);
	else if (!strcmp(mode, "del"))
		handle_del(jobid, endpoint);
	else if (!strcmp(mode, "output"))
		handle_output(jobid, endpoint);
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
	cout << "  'status':" << endl;
	cout << "    -f|--jidfile [Job identifier file] Job identifier file to use (#)" << endl;
	cout << endl;
	cout << "  *: argument is mandatory" << endl;
	cout << "  #: mandatory when '-j' isn't set" << endl;
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
 * Parse input files
 * @param[out] job G3BridgeType__Job object to fill in
 * @param inputs inputs string to parse
 */
void parse_inputs(G3BridgeType__Job &job, const char *inputs)
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
		G3BridgeType__LogicalFile *lf = new G3BridgeType__LogicalFile;
		lf->logicalName = names.at(0);
		lf->URL = names.at(1);
		job.inputs.push_back(lf);
	}
}


/**
 * Parse output files
 * @param[out] job G3BridgeType__Job object to fill in
 * @param outputs outputs string to parse
 */
void parse_outputs(G3BridgeType__Job &job, const char *outputs)
{
	split_by_delims(outputs, ",", job.outputs);
}


/**
 * Handle submission
 * @param job G3BridgeType__Job object to send to the Service
 * @param endpoint Service endpoint to use
 */
void handle_add(G3BridgeType__Job &job, const char *endpoint)
{
	if ("" == job.alg || "" == job.grid)
	{
		cerr << "Job addition problem: either name or grid undefined!" << endl;
		help_screen(true);
	}

	struct G3BridgeOp__submitResponse IDs;
	G3BridgeType__JobList jList;
	jList.job.clear();
	jList.job.push_back(&job);

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call_G3BridgeOp__submit(soap, endpoint, NULL, &jList, IDs))
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
 * Check if job identifier is not empty of if yes, the provided job id file is
 * valid. Exits when there is a problem
 * @param val string to check
 * @param fname file existence to check
 */
void check_jobid(const char *val, const char *fname = NULL)
{
	if (val && strlen(val))
		return;

	if (fname)
	{
		ifstream jidfile(fname);
		if (jidfile)
		{
			jidfile.close();
			return;
		}
	}

	if (!fname)
		cerr << "Error: job identifier isn't set!" << endl;
	else
		cerr << "Error: neither job identifier and job identifier file are set!" << endl;
	help_screen(true);
}


/**
 * Handle status query
 * @param jobid job identifier we would like to use
 * @param endpoint Service endpoint to use
 */
void handle_status(const char *jobid, const char *jidfname, const char *endpoint)
{
	G3BridgeType__JobIDList jList;
	struct G3BridgeOp__getStatusResponse resp;
	struct {
		G3BridgeType__JobStatus st;
		string str;
	} statToStr[] = {
		{G3BridgeType__JobStatus__UNKNOWN,  "Unknown" },
		{G3BridgeType__JobStatus__INIT,     "Init"    },
		{G3BridgeType__JobStatus__RUNNING,  "Running" },
		{G3BridgeType__JobStatus__FINISHED, "Finished"},
		{G3BridgeType__JobStatus__ERROR,    "Error"   }
	};

	check_jobid(jobid, jidfname);

	jList.jobid.clear();
	if (jobid)
		jList.jobid.push_back(string(jobid));
	if (jidfname)
	{
		ifstream idfile(jidfname);
		if (idfile)
		{
			string idread;
			idfile >> idread;
			while (!idfile.eof())
			{
				jList.jobid.push_back(idread);
				idfile >> idread;
			}
			idfile.close();
		}
	}

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call_G3BridgeOp__getStatus(soap, endpoint, NULL, &jList, resp))
	{
		soap_print_fault(soap, stderr);
		exit(-1);
	}

	for (unsigned i = 0; i < jList.jobid.size(); i++)
	{
		G3BridgeType__JobStatus st = resp.statuses->status.at(i);
		cout << jList.jobid.at(i) << " " << statToStr[st].str << endl;
	}

	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);
}


/**
 * Handle job delete
 * @param jobid job identifier we would like to use
 * @param endpoint Service endpoint to use
 */
void handle_del(const char *jobid, const char *endpoint)
{
	G3BridgeType__JobIDList jList;
	struct G3BridgeOp__delJobResponse resp;

	check_jobid(jobid);
	jList.jobid.clear();
	jList.jobid.push_back(string(jobid));

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call_G3BridgeOp__delJob(soap, endpoint, NULL, &jList, resp))
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
	G3BridgeType__JobIDList jList;
	struct G3BridgeOp__getOutputResponse resp;

	check_jobid(jobid);
	jList.jobid.clear();
	jList.jobid.push_back(string(jobid));

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call_G3BridgeOp__getOutput(soap, endpoint, NULL, &jList, resp))
	{
		soap_print_fault(soap, stderr);
		exit(-1);
	}

	G3BridgeType__JobOutput *jout = resp.outputs->output.at(0);
	cout << "Output files for job \"" << jout->jobid << "\":" << endl;
	for (unsigned i = 0; i < jout->output.size(); i++)
	{
		G3BridgeType__LogicalFile *lf = jout->output.at(i);
		cout << "    " << lf->logicalName << " - " << lf->URL << endl;
	}

	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);
}
