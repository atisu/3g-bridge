#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <getopt.h>
#include <string.h>

#include <vector>
#include <fstream>
#include <iostream>

#include "soap/soapH.h"
#include "soap/G3BridgeSubmitter.nsmap"

#include <glib.h>

using namespace std;

/**********************************************************************
 * Data type definitions
 */

typedef enum { ADD, DELETE, STATUS, OUTPUT, FINISHED } op_mode;


/**********************************************************************
 * Prototypes
 */

static int add_jobid(const char *name, const char *value, void *ptr, GError **error);
static void handle_add(void);
static void handle_status(void);
static void handle_delete(void);
static void handle_output(void);
static void handle_finished(void);


/**********************************************************************
 * Global variables
 */

static char *mode;
static char *endpoint;
static char *name;
static char *grid;
static char *args;
static char **inputs;
static char **outputs;
static G3Bridge__JobIDList jobIDs;
static char *jidfile;


static GOptionEntry options[] =
{
	{ "endpoint",		'e',	0,	G_OPTION_ARG_STRING,		&endpoint,
		"Service endpoint", "URL" },
	{ "mode",		'm',	0,	G_OPTION_ARG_STRING,		&mode,
		"Operation mode", "(add|status|delete|output|finished)" },
	{ NULL }
};

static GOptionEntry add_options[] =
{
	{ "name",		'n',	0,	G_OPTION_ARG_STRING,		&name,
		"Name of the algorithm to execute", "NAME" },
	{ "grid",		'g',	0,	G_OPTION_ARG_STRING,		&grid,
		"Grid where to submit the job to", "NAME" },
	{ "args",		'a',	0,	G_OPTION_ARG_STRING,		&args,
		"Command-line arguments for the job", "ARGS" },
	{ "in",			'i',	0,	G_OPTION_ARG_STRING_ARRAY,	&inputs,
		"Input file specification", "NAME=URL" },
	{ "out",		'o',	0,	G_OPTION_ARG_STRING_ARRAY,	&outputs,
		"Output file names", "NAME" },
	{ NULL }
};

static GOptionEntry other_options[] =
{
	{ "jid",		'j',	0,	G_OPTION_ARG_CALLBACK,		(void *)&add_jobid,
		"Job identifier", "UUID" },
	{ "jidfile",		'f',	0,	G_OPTION_ARG_FILENAME,		&jidfile,
		"Input file holding the job identifiers. '-' can be used to read the "
		"job IDs from STDIN", "FILE" },
	{ NULL }
};

/**********************************************************************
 * Main program
 */

int main(int argc, char **argv)
{
	GOptionContext *context;
	GOptionGroup *group;
	GError *error = NULL;

	context = g_option_context_new("- Simple client for the 3G Submitter service");
	g_option_context_add_main_entries(context, options, PACKAGE);
	group = g_option_group_new("add", "Job submission options:",
		"Options for submitting a new job", NULL, NULL);
	g_option_group_add_entries(group, add_options);
	g_option_context_add_group(context, group);
	group = g_option_group_new("other", "Query/manipulation options:",
		"Options for dealing with submitted jobs", NULL, NULL);
	g_option_group_add_entries(group, other_options);
	g_option_context_add_group(context, group);

        if (!g_option_context_parse(context, &argc, &argv, &error))
	{
		cerr << "Error: " << error->message << endl;
		exit(1);
	}

	if (!mode)
	{
		cerr << "Error: no mode selected!" << endl;
		exit(1);
	}

	if (!endpoint)
	{
		cerr << "Error: no endpoint URL specified!" << endl;
		exit(1);
	}

	op_mode m;
	if (!strncmp(mode, "add", strlen(mode)))
		m = ADD;
	else if (!strncmp(mode, "status", strlen(mode)))
		m = STATUS;
	else if (!strncmp(mode, "delete", strlen(mode)))
		m = DELETE;
	else if (!strncmp(mode, "output", strlen(mode)))
		m = OUTPUT;
	else if (!strncmp(mode, "finished", strlen(mode)))
		m = FINISHED;
	else
	{
		cerr << "Error: invalid mode specified!" << endl;
		exit(1);
	}

	switch (m)
	{
		case ADD:
			handle_add();
			return 0;
		case FINISHED:
			handle_finished();
			return 0;
		default:
			break;
	}

	/* For 'status', 'delete' and 'outputs', read the list of job IDs first */
	if (jidfile)
	{
		char buf[64], *p;
		FILE *f;

		if (!strcmp(jidfile, "-"))
			f = stdin;
		else
		{
			f = fopen(jidfile, "r");
			if (!f)
			{
				fprintf(stderr, "Failed to open %s: %s\n",
					jidfile, strerror(errno));
				exit(1);
			}
		}

		while (fgets(buf, sizeof(buf), f))
		{
			p = strchr(buf, '\n');
			if (p)
				*p = '\0';
			p = buf;
			if (!buf[0] || buf[0] == '#')
				continue;

			jobIDs.jobid.push_back(buf);
		}

		if (f != stdin)
			fclose(f);
	}

	switch (m)
	{
		case OUTPUT:
			handle_output();
			break;
		case STATUS:
			handle_status();
			break;
		case DELETE:
			handle_delete();
			break;
		default:
			break;
	}

	return(0);
}


/**
 * Job ID addition callback function
 */
static int add_jobid(const char *name, const char *value, void *ptr, GError **error)
{
	jobIDs.jobid.push_back(value);
	return TRUE;
}


/**
 * Handle submission
 */
static void handle_add(void)
{
	G3Bridge__Job job;

	if (!name || !grid)
	{
		cerr << "Job addition problem: either name or grid undefined!" << endl;
		exit(1);
	}

	job.alg = name;
	job.grid = grid;

	struct soap *soap = soap_new();
	soap_init(soap);

	for (unsigned i = 0; inputs && inputs[i]; i++)
	{
		char *p;

		p = strchr(inputs[i], '=');
		if (!p)
		{
			cerr << "Malformed input definition string: " << inputs[i] << endl;
			exit(1);
		}

		*p++ = '\0';
		if (!strlen(p))
		{
			cerr << "Input URL is missing for " << inputs[i] << endl;
			exit(1);
		}

		G3Bridge__LogicalFile *lf = soap_new_G3Bridge__LogicalFile(soap, -1);
		lf->logicalName = inputs[i];
		lf->URL = p;
		job.inputs.push_back(lf);
	}

	for (unsigned i = 0; outputs && outputs[i]; i++)
		job.outputs.push_back(outputs[i]);

	G3Bridge__JobIDList IDs;
	G3Bridge__JobList jList;
	jList.job.clear();
	jList.job.push_back(&job);

	if (SOAP_OK != soap_call___G3Bridge__submit(soap, endpoint, NULL, &jList, &IDs))
	{
		soap_print_fault(soap, stderr);
		exit(-1);
	}
	cout << "The job ID is: " << IDs.jobid.at(0) << endl;
	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);
}


/**
 * Handle status query
 */
static void handle_status(void)
{
	G3Bridge__StatusList resp;
	struct {
		G3Bridge__JobStatus st;
		string str;
	} statToStr[] = {
		{G3Bridge__JobStatus__UNKNOWN,  "Unknown" },
		{G3Bridge__JobStatus__INIT,     "Init"    },
		{G3Bridge__JobStatus__RUNNING,  "Running" },
		{G3Bridge__JobStatus__FINISHED, "Finished"},
		{G3Bridge__JobStatus__ERROR,    "Error"   }
	};

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call___G3Bridge__getStatus(soap, endpoint, NULL, &jobIDs, &resp))
	{
		soap_print_fault(soap, stderr);
		exit(-1);
	}

	for (unsigned i = 0; i < jobIDs.jobid.size(); i++)
	{
		G3Bridge__JobStatus st = resp.status.at(i);
		cout << jobIDs.jobid.at(i) << " " << statToStr[st].str << endl;
	}

	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);
}


/**
 * Handle job delete
 */
static void handle_delete(void)
{
	struct __G3Bridge__deleteResponse resp;

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call___G3Bridge__delete(soap, endpoint, NULL, &jobIDs, resp))
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
 */
static void handle_output(void)
{
	G3Bridge__OutputList resp;

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call___G3Bridge__getOutput(soap, endpoint, NULL, &jobIDs, &resp))
	{
		soap_print_fault(soap, stderr);
		exit(-1);
	}

	for (unsigned i = 0; i < jobIDs.jobid.size(); i++)
	{
		G3Bridge__JobOutput *jout = resp.output.at(i);
		cout << "# Output files for job \"" << jobIDs.jobid.at(i) << "\":" << endl;
		for (unsigned j = 0; j < jout->output.size(); j++)
		{
			G3Bridge__LogicalFile *lf = jout->output.at(j);
			cout << lf->logicalName << "\t" << lf->URL << endl;
		}
	}

	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);
}

/**
 * Query the list of finished jobs
 */
static void handle_finished(void)
{
	G3Bridge__JobIDList resp;

	if (!grid)
	{
		cerr << "Error: the grid name is not specified!" << endl;
		exit(1);
	}

	struct soap *soap = soap_new();
	soap_init(soap);
	if (SOAP_OK != soap_call___G3Bridge__getFinished(soap, endpoint, NULL, grid, &resp))
	{
		soap_print_fault(soap, stderr);
		exit(-1);
	}

	for (unsigned i = 0; i < resp.jobid.size(); i++)
		cout << resp.jobid.at(i) << endl;

	soap_destroy(soap);
	soap_end(soap);
	soap_done(soap);
}
