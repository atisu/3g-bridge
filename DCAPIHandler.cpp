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
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define SCRIPT_NAME		"script"
#define INPUT_DIR		"inputs"
#define INPUT_NAME		"inputs.tar.gz"
#define OUTPUT_DIR		"outputs"
#define OUTPUT_NAME		"outputs.tar.gz"
#define OUTPUT_PATTERN		"outputs/*"

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

static void invoke_cmd(const char *exe, const char *const argv[]) throw (BackendException &)
{
	pid_t pid = fork();
	if (pid)
	{
		/* Parent */
		int status;

		if (waitpid(pid, &status, 0) == -1)
			throw new BackendException(string("waitpid() failed: ") + strerror(errno));
		if (!WIFEXITED(status))
			throw new BackendException(string("Command ") + exe + " died");
		if (WEXITSTATUS(status))
			throw new BackendException(string("Command ") + exe + " exited with non-zero status");
		return;
	}

	/* Child */
	int fd = open("/dev/null", O_RDWR);
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	close(fd);

	execvp(exe, (char *const *)argv);
	_Exit(255);
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

static string setup_workdir(void) throw (BackendException &)
{
	char buf[PATH_MAX];

	snprintf(buf, sizeof(buf), "batch_XXXXXX");
	if (!mkdtemp(buf))
		throw new BackendException(string("Failed to create directory: ") + strerror(errno));

	return string(buf);
}

static void emit_job(CGJob *job, const string &basedir, ofstream &script, const string &job_template) throw (BackendException &)
{
	string input_dir = INPUT_DIR "/" + job->getId();
	string output_dir = OUTPUT_DIR "/" + job->getId();

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
		{
			const char *cp_args[] =
				{ "cp", "-p", src.c_str(), dst.c_str(), 0 };
			invoke_cmd("cp", cp_args);
		}
	}

	string tmpl = substitute(job_template, "input_dir", input_dir);
	tmpl = substitute(tmpl, "output_dir", output_dir);
	tmpl = substitute(tmpl, "args", job->getArgs());

	script << tmpl;
}

void DCAPIHandler::submitJobs(vector<CGJob *> *jobs) throw (BackendException &)
{
	char input_name[PATH_MAX] = { 0 };
	BackendException *err;
	DC_Workunit *wu;
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

	try
	{
		basedir = setup_workdir();
		string script_name = basedir + "/" SCRIPT_NAME;
		ofstream script(script_name.c_str(), ios::out);
		script << substitute(head_template, "inputs", INPUT_NAME);

		double total = jobs->size();
		int cnt = 0;
		for (vector<CGJob *>::const_iterator it = jobs->begin(); it != jobs->end(); it++, cnt++)
		{
			char cmd[128];

			emit_job(*it, basedir, script, job_template);
			snprintf(cmd, sizeof(cmd), "boinc fraction_done %.6g\n", total / cnt);
			script << cmd;
		}

		snprintf(input_name, sizeof(input_name), "inputs_XXXXXX");
		int fd = mkstemp(input_name);
		if (fd == -1)
			throw new BackendException(string("Failed to create file: ") + strerror(errno));
		close(fd);

		const char *tarargs[] =
		{
			"tar", "-C", basedir.c_str(), "-c", "-z", "-f", input_name, INPUT_DIR, OUTPUT_DIR, 0
		};
		invoke_cmd("tar", (char *const *)tarargs);

		const char *wu_args[] = { SCRIPT_NAME, 0 };
		wu = DC_createWU(algname.c_str(), wu_args, 0, NULL);
		if (!wu)
			throw new BackendException("Out of memory");

		DC_addWUInput(wu, INPUT_NAME, input_name, DC_FILE_VOLATILE);
		DC_addWUInput(wu, SCRIPT_NAME, script_name.c_str(), DC_FILE_VOLATILE);
		DC_addWUOutput(wu, OUTPUT_NAME);

		if (DC_submitWU(wu))
			throw new BackendException("WU submission failed");

		char *wu_id = DC_getWUId(wu);
		for (vector<CGJob *>::const_iterator it = jobs->begin(); it != jobs->end(); it++)
		{
			(*it)->setGridId(wu_id);
			(*it)->setStatus(CG_RUNNING);
		}
		free(wu_id);

		tail_template = substitute(tail_template, "outputs", OUTPUT_NAME);
		tail_template = substitute(tail_template, "output_pattern", OUTPUT_PATTERN);
		script << tail_template;
	}
	catch (BackendException &e)
	{
		err = &e;

		if (input_name[0])
			unlink(input_name);
		if (wu)
			DC_destroyWU(wu);
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
#if 0
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
#endif
}
