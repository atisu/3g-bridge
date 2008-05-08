#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGJob.h"
#include "DCAPIHandler.h"

#include <dc.h>

#include <vector>
#include <iostream>
#include <fstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define SCRIPT_NAME		"script"
#define INPUTS_DIR		"inputs"
#define INPUTS_NAME		"inputs.tar.gz"
#define OUTPUTS_DIR		"outputs"
#define OUTPUTS_NAME		"outputs.tar.gz"
#define OUTPUTS_PATTERN		"outputs/*"

using namespace std;

static string load_file(const string name)
{
	ifstream file(name.c_str(), ios::in);
	string result, line;
	while (getline(file, line))
		result += line + "\n";
	return result;
}

static string substitute(const string src, const string key, string value)
{
	string result = src;
	size_t begin = 0, end;
	for (;;)
	{
		begin = result.find("%{", begin);
		if (begin == string::npos)
			return result;
		end = result.find('}', begin + 2);
		if (!result.compare(begin + 2, end - begin - 3, key))
			result = result.substr(0, begin) + value + result.substr(end + 1);
	}
}

static string get_dc_client_config(const string &app, const char *key, bool strict = false)
{
	char *value = DC_getClientCfgStr(app.c_str(), key, 0);

	if (strict && !value)
		throw string("Configuration error: missing ") + key + " for app " + app;
	string str(value);
	free(value);
	return str;
}


DCAPIHandler::DCAPIHandler(const string conf)
{
	if (DC_OK != DC_initMaster(conf.c_str()))
		throw DC_initMasterError;
}


DCAPIHandler::~DCAPIHandler()
{
}

static string setup_workdir(void)
{
	char buf[PATH_MAX];

	snprintf(buf, sizeof(buf), "batch_XXXXXX");
	if (!mkdtemp(buf))
	{
		/* XXX Log */
		throw errno;
	}

	return string(buf);
}

static void emit_job(CGJob *job, const string &basedir, ofstream &script, const string &job_template) throw (BackendException &)
{
	string input_dir = INPUTS_DIR "/" + job->getId();
	string output_dir = OUTPUTS_DIR "/" + job->getId();

	string input_path = basedir + "/" + input_dir;
	if (mkdir(input_path.c_str(), 0750))
		throw new BackendException("Failed to create directory " + input_path +
			": " + strerror(errno));

	string output_path = basedir + "/" + output_dir;
	if (mkdir(output_path.c_str(), 0750))
		throw new BackendException("Failed to create directory " + output_path +
			": " + strerror(errno));

	/* Link/copy the input files to the proper location */
	vector<string> inputs = job->getInputs();
	for (vector<string>::const_iterator it = inputs.begin(); it != inputs.end(); it++)
	{
		string src = job->getInputPath(*it);
		string dst = input_path + "/" + *it;

		/* First, try to create a hard link; if that fails, do a copy */
		if (link(src.c_str(), dst.c_str()))
			copy_file(src, dst);
	}

	string tmpl = substitute(job_template, "input_dir", input_dir);
	tmpl = substitute(tmpl, "output_dir", output_dir);
	tmpl = substitute(tmpl, "args", job->getArgs());

	script << tmpl;
}

static void submit_internal(vector<CGJob *> *jobs, const string &basedir, ofstream &script, const string &job_template) throw (BackendException &)
{
	snprintf(input_name, sizeof(input_name), "inputs_XXXXXX");
	int fd = mkstemp(input_name);
	if (fd == -1)
		throw new BackendException(string("Failed to create input file: ") + strerror(errno));
	close(fd);

	string cmd("tar -C '" + basedir + "' -c -z -f '" + input_name + " " INPUTS_DIR + " " + OUTPUTS_DIR);
	system(cmd);

	const char *args[1];
	args[0] = SCRIPT_NAME;
	args[1] = NULL;
	wu = DC_createWU(algname.c_str(), args, 0, NULL);
	if (!wu)
		throw new BackendException("Out of memory");

	DC_addWUInput(wu, INPUTS_NAME, input_name);
	DC_addWUOutput(wu, OUTPUTS_NAME);

	DC_submitWU(wu);

	char *wu_id = DC_getWUId(wu);
	for (vector<CGJob *>::const_iterator it = jobs->begin(); it != jobs->end(); it++)
	{
		job->setGridId(wu_id);
		job->setStatus(CG_RUNNING);
	}
	free(wu_id);
}

void DCAPIHandler::submitJobs(vector<CGJob *> *jobs) throw (BackendException &)
{
	BackendException *err;
	string basedir;
	int ret;

	/* First, sanity check: all jobs must belong to the same alg */
	vector<CGJob *>::const_iterator i = jobs->begin();
	string algname = (*i)->getAlgorithm()->getName();
	while (i != jobs->end())
	{
		if (algname != (*i)->getAlgorithm()->getName())
			throw "Multiple algorithms cannot be in the same batch";
	}

	string head_template = load_file(get_dc_client_config(algname, "BatchHeadTemplate", true));
	string job_template = load_file(get_dc_client_config(algname, "BatchJobTemplate", true));
	string tail_template = load_file(get_dc_client_config(algname, "BatchTailTemplate", true));

	basedir = setup_workdir();
	ofstream script((basedir + "/" SCRIPT_NAME).c_str(), ios::out);
	script << substitute(head_template, "inputs", INPUTS_NAME);

	try
	{
		double total = jobs->size();
		for (vector<CGJob *>::const_iterator it = jobs->begin(), int i = 0; it != jobs->end(); it++, i++)
		{
			char cmd[128];

			emit_job(*it, basedir, script, job_template);
			snprintf(cmd, sizeof(cmd), "boinc fraction_done %.6g\n", total / i);
			script << cmd;
		}

		submit_internal(jobs, basedir, script, job_template);

		tail_template = substitute(tail_template, "outputs", OUTPUTS_NAME);
		tail_template = substitute(tail_template, "output_pattern", OUTPUT_PATTERN);
		script << tail_template;
	}
	catch (BackendException &e)
	{
		err = e;
	}

	/* Remove the temp. directory */
	char cmd[PATH_MAX];

	snprintf(cmd, sizeof(cmd), "rm -rf %s", basedir.c_str());
	system(cmd);

	if (err)
		throw err;
}


void DCAPIHandler::updateStatus(void) throw (BackendException &)
{
	DC_MasterEvent *event;
	char *wutag;
	uuid_t *id = new uuid_t[1];
	vector<string> outputs;
	string localname, outfilename, algname, workdir;
	struct stat stFileInfo;

	// Process pending events
	while (NULL != (event = DC_waitMasterEvent(NULL, 0))) {
		// Throw non-result events
		if (event->type != DC_MASTER_RESULT)
		{
			DC_destroyMasterEvent(event);
			continue;
		}
		// Now get the WU tag of the event
		wutag = DC_getWUTag(event->wu);
		if (uuid_parse(wutag, *id) == -1)
		{
			cerr << "Failed to parse uuid" << endl;
			DC_destroyWU(event->wu);
			DC_destroyMasterEvent(event);
			continue;
		}
		// Search for the id in the received jobs
		for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++)
		{
			CGJob *job = *it;
			if (job->getGridID() == (string)wutag)
			{
				if (!event->result)
					job->setStatus(CG_ERROR);
				else
					job->setStatus(CG_FINISHED);
				DC_destroyMasterEvent(event);
			}
		}
	}
}
