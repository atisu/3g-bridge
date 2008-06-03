#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "CGJob.h"
#include "DCAPIHandler.h"
#include "DBHandler.h"
#include "Logging.h"

#include <dc.h>

#include <vector>
#include <iostream>
#include <fstream>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#define SCRIPT_NAME		"script"
#define INPUT_DIR		"inputs"
#define INPUT_NAME		"inputs.pack"
#define OUTPUT_DIR		"outputs"
#define OUTPUT_NAME		"outputs.pack"
#define OUTPUT_PATTERN		"outputs/*"

/* Forward declarations */
static void result_callback(DC_Workunit *wu, DC_Result *res);

/* This should be a class member but it has to be referenced from a C callback
 * so it must be global */
static DBHandler *dbh;

using namespace std;

/**********************************************************************
 * Utility functions
 */

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
		if (!result.compare(begin + 2, end - begin - 2, key))
			result = result.substr(0, begin) + value + result.substr(end + 1);
		else
			begin = end;
	}
}

static void invoke_cmd(const char *exe, const char *const argv[]) throw (BackendException &)
{
	int sock[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock) == -1)
		throw BackendException("socketpair() failed: %s", strerror(errno));

	pid_t pid = fork();
	if (pid)
	{
		/* Parent */
		int status;
		string error;
		char buf[1024];

		close(sock[0]);
		while ((status = read(sock[1], buf, sizeof(buf))) > 0)
			error.append(buf, status);
		close(sock[1]);

		if (waitpid(pid, &status, 0) == -1)
			throw BackendException("waitpid() failed: %s", strerror(errno));
		if (WIFSIGNALED(status))
			throw BackendException("Command %s died with signal %d [%s]",
				exe, WTERMSIG(status), error.c_str());
		if (WEXITSTATUS(status))
			throw BackendException("Command %s exited with status %d [%s]",
				exe, WEXITSTATUS(status), error.c_str());
		return;
	}

	/* Child */
	int fd = open("/dev/null", O_RDWR);
	dup2(fd, 0);
	dup2(fd, 1);
	close(fd);

	dup2(sock[0], 2);
	close(sock[0]);
	close(sock[1]);

	execvp(exe, (char *const *)argv);
	_Exit(255);
}

static string get_dc_client_config(const string &app, const char *key, bool strict = false)
{
	char *value = DC_getClientCfgStr(app.c_str(), key, 1);

	if (strict && !value)
		throw BackendException("Configuration error: key %s is missing for app %s", key, app.c_str());
	string str(value);
	free(value);
	return str;
}

static string abspath(string path) throw (BackendException &)
{
	if (!path.length())
		return path;
	if (path[0] == '/')
		return path;

	char *tmp = DC_getCfgStr("WorkingDirectory");
	if (!tmp)
		throw BackendException("DC-API working directory is not configured?!?");
	path = string(tmp) + "/" + path;
	free(tmp);
	return path;
}

static string create_tmpdir(void) throw (BackendException &)
{
	char buf[PATH_MAX];

	char *workdir = DC_getCfgStr("WorkingDirectory");
	if (!workdir)
		throw BackendException("DC-API working directory is not configured?!?");

	snprintf(buf, sizeof(buf), "%s/batch_XXXXXX", workdir);
	free(workdir);
	if (!mkdtemp(buf))
		throw BackendException("Failed to create directory '%s': %s", buf, strerror(errno));

	return string(buf);
}

static void remove_tmpdir(const string &dir) throw (BackendException &)
{
	if (!dir.size())
		return;

	const char *args[] = { "rm", "-rf", dir.c_str(), 0 };
	invoke_cmd("/bin/rm", args);
}

static void error_jobs(vector<CGJob *> *jobs)
{
	for (vector<CGJob *>::const_iterator it = jobs->begin(); it != jobs->end(); it++)
	{
		(*it)->setStatus(ERROR);
		delete *it;
	}
	delete jobs;
}

static void delete_vector(vector<CGJob *> *jobs)
{
	for (vector<CGJob *>::const_iterator it = jobs->begin(); it != jobs->end(); it++)
		delete *it;
	delete jobs;
}

static bool check_job(const string &basedir, CGJob *job)
{
	vector<string> outputs = job->getOutputs();
	bool result = true;

	/* Move/copy the output files to the proper location */
	string jobdir = basedir + "/" OUTPUT_DIR "/" + job->getId();
	for (vector<string>::const_iterator it = outputs.begin(); it != outputs.end(); it++)
	{
		string src = jobdir + '/' + *it;
		string dst = job->getOutputPath(*it);

		if (!rename(src.c_str(), dst.c_str()))
			continue;
		if (errno == ENOENT)
		{
			LOG(LOG_ERR, "Job %s: Output file '%s' is missing", job->getId().c_str(), it->c_str());
			result = false;
			continue;
		}
		else if (errno != EXDEV)
		{
			LOG(LOG_ERR, "Job %s: Failed to rename output file '%s' to '%s': %s",
				job->getId().c_str(), src.c_str(), dst.c_str(), strerror(errno));
			result = false;
			continue;
		}

		/* Source and destination are on different file systems, call
		 * external mv command */
		const char *mv_args[] = { "mv", "-f", src.c_str(), dst.c_str(), 0 };
		try
		{
			invoke_cmd("/bin/mv", mv_args);
		}
		catch (QMException &e)
		{
			LOG(LOG_ERR, "Job %s: Failed to move output file '%s' to '%s'",
				job->getId().c_str(), src.c_str(), dst.c_str());
			result = false;
		}
	}
	return result;
}

static void result_callback(DC_Workunit *wu, DC_Result *result)
{
	char *tmp = DC_getWUId(wu);
	string id(tmp);
	free(tmp);

	tmp = DC_getWUTag(wu);
	string tag(tmp);
	free(tmp);

	vector<CGJob *> *jobs = dbh->getJobs(id.c_str());

	if (!result)
	{
		LOG(LOG_ERR, "DC-API: WU %s: Failed", id.c_str());
		error_jobs(jobs);
		DC_destroyWU(wu);
		return;
	}

	tmp = DC_getResultOutput(result, OUTPUT_NAME);
	if (!tmp)
	{
		LOG(LOG_ERR, "DC-API: WU %s: Missing output", id.c_str());
		error_jobs(jobs);
		DC_destroyWU(wu);
		return;
	}
	string outputs(tmp);
	free(tmp);

	LOG(LOG_INFO, "DC-API: Received result for WU %s (app '%s')",
		id.c_str(), tag.c_str());

	string basedir = create_tmpdir();
	string unpack_script = abspath(get_dc_client_config(tag.c_str(), "BatchUnpackScript", true));

	try
	{
		const char *unpack_args[] =
		{
			unpack_script.c_str(), outputs.c_str(), basedir.c_str(), 0
		};
		invoke_cmd(unpack_script.c_str(), unpack_args);

		for (vector<CGJob *>::const_iterator it = jobs->begin(); it != jobs->end(); it++)
		{
			/* If the job is already marked as cancelled, just
			 * delete it */
			if ((*it)->getStatus() == CANCEL)
			{
				(*it)->deleteJob();
				continue;
			}

			if (check_job(basedir, *it))
				(*it)->setStatus(FINISHED);
			else
				(*it)->setStatus(ERROR);
		}
	}
	catch (BackendException &e)
	{
		remove_tmpdir(basedir.c_str());
		delete_vector(jobs);
		DC_destroyWU(wu);
		throw;
	}

	remove_tmpdir(basedir.c_str());
	delete_vector(jobs);
	DC_destroyWU(wu);
}

/**********************************************************************
 * Class: DCAPIHandler
 */

DCAPIHandler::DCAPIHandler(DBHandler *jobdb, QMConfig &config)
{
	string conffile = config.getStr("DCAPI_Config");

	if (DC_OK != DC_initMaster(conffile.length() ? conffile.c_str(): NULL))
		throw BackendException("Failed to initialize the DC-API");

	DC_setMasterCb(result_callback, NULL, NULL);

	dbh = jobdb;

	groupByNames = true;
	maxGroupSize = 100;
}


DCAPIHandler::~DCAPIHandler()
{
	dbh = 0;
}

static void do_mkdir(const string &path) throw (BackendException &)
{
	if (!mkdir(path.c_str(), 0750) || errno == EEXIST)
		return;

	throw BackendException("Failed to create directory '%s': %s", path.c_str(), strerror(errno));
}

static void emit_job(CGJob *job, const string &basedir, ofstream *script, const string &job_template) throw (BackendException &)
{
	string input_dir = INPUT_DIR "/" + job->getId();
	string output_dir = OUTPUT_DIR "/" + job->getId();

	string input_path = basedir + "/" + input_dir;
	do_mkdir(input_path);

	string output_path = basedir + "/" + output_dir;
	do_mkdir(output_path);

	/* Link/copy the input files to the proper location */
	vector<string> inputs = job->getInputs();
	for (vector<string>::const_iterator it = inputs.begin(); it != inputs.end(); it++)
	{
		string src = job->getInputPath(*it);
		string dst = input_path + "/" + *it;

		/* First, try to create a hard link; if that fails, do a copy */
		if (link(src.c_str(), dst.c_str()))
		{
			const char *cp_args[] = { "cp", "-p", src.c_str(), dst.c_str(), 0 };
			invoke_cmd("cp", cp_args);
		}
	}

	string tmpl = substitute(job_template, "input_dir", input_dir);
	tmpl = substitute(tmpl, "output_dir", output_dir);
	tmpl = substitute(tmpl, "args", job->getArgs());

	*script << tmpl;
}

void DCAPIHandler::submitJobs(vector<CGJob *> *jobs) throw (BackendException &)
{
	char input_name[PATH_MAX] = { 0 };
	DC_Workunit *wu = 0;
	string basedir;

	/* First, sanity check: all jobs must belong to the same alg */
	vector<CGJob *>::const_iterator i = jobs->begin();
	if (i == jobs->end())
		return;

	string algname = (*i)->getAlgQueue()->getName();
	while (i != jobs->end())
	{
		if (algname != (*i)->getAlgQueue()->getName())
			throw BackendException("Multiple algorithms cannot be in the same batch");
		i++;
	}

	string head_template = load_file(abspath(get_dc_client_config(algname, "BatchHeadTemplate", true)));
	string job_template = load_file(abspath(get_dc_client_config(algname, "BatchBodyTemplate", true)));
	string tail_template = load_file(abspath(get_dc_client_config(algname, "BatchTailTemplate", true)));
	string pack_script = abspath(get_dc_client_config(algname, "BatchPackScript", true));

	basedir = create_tmpdir();
	string script_name = basedir + "/" SCRIPT_NAME;
	ofstream *script = 0;

	try
	{
		script = new ofstream(script_name.c_str(), ios::out);
		if (script->fail())
			throw BackendException("Failed to create '%s': %s", script_name.c_str(),
				strerror(errno));

		*script << substitute(head_template, "inputs", INPUT_NAME);

		string dir = basedir + "/" INPUT_DIR;
		do_mkdir(dir);
		dir = basedir + "/" OUTPUT_DIR;
		do_mkdir(dir);

		double total = jobs->size();
		int cnt = 0;
		for (vector<CGJob *>::const_iterator it = jobs->begin(); it != jobs->end(); it++)
		{
			char cmd[128];

			emit_job(*it, basedir, script, job_template);
			snprintf(cmd, sizeof(cmd), "boinc fraction_done %.6g\n", (double)++cnt / total);
			*script << cmd;
		}

		tail_template = substitute(tail_template, "outputs", OUTPUT_NAME);
		tail_template = substitute(tail_template, "output_pattern", OUTPUT_PATTERN);
		*script << tail_template;

		script->close();
		delete script;
		script = 0;

		snprintf(input_name, sizeof(input_name), "inputs_XXXXXX");
		int fd = mkstemp(input_name);
		if (fd == -1)
			throw BackendException("Failed to create file '%s': %s", input_name, strerror(errno));
		close(fd);

		const char *pack_args[] =
		{
			pack_script.c_str(), basedir.c_str(), input_name, INPUT_DIR, OUTPUT_DIR, 0
		};
		invoke_cmd(pack_script.c_str(), (char *const *)pack_args);

		/* mkstemp() creates the file with 0600 */
		chmod(input_name, 0644);

		const char *wu_args[] = { SCRIPT_NAME, 0 };
		wu = DC_createWU(algname.c_str(), wu_args, 0, algname.c_str());
		if (!wu)
		{
			unlink(input_name);
			throw BackendException("Out of memory");
		}

		if (DC_addWUInput(wu, INPUT_NAME, input_name, DC_FILE_VOLATILE))
		{
			unlink(input_name);
			throw BackendException("Failed to add input file to WU");
		}
		if (DC_addWUInput(wu, SCRIPT_NAME, script_name.c_str(), DC_FILE_VOLATILE))
			throw BackendException("Failed to add input file to WU");
		if (DC_addWUOutput(wu, OUTPUT_NAME))
			throw BackendException("Failed to add output file to WU");

		if (DC_submitWU(wu))
			throw BackendException("WU submission failed");

		char *wu_id = DC_getWUId(wu);
		LOG(LOG_INFO, "DC-API: Submitted work unit %s for app '%s' (%zd tasks)",
			wu_id, algname.c_str(), jobs->size());

		for (vector<CGJob *>::const_iterator it = jobs->begin(); it != jobs->end(); it++)
		{
			(*it)->setGridId(wu_id);
			(*it)->setStatus(RUNNING);
		}
		free(wu_id);

	}
	catch (BackendException &e)
	{
		if (input_name[0])
			unlink(input_name);
		if (wu)
			DC_destroyWU(wu);
		if (script)
		{
			script->close();
			delete script;
		}
		remove_tmpdir(basedir.c_str());
		throw;
	}

	if (script)
	{
		script->close();
		delete script;
	}
	remove_tmpdir(basedir.c_str());
}


void DCAPIHandler::updateStatus(void) throw (BackendException &)
{
	int ret = DC_processMasterEvents(0);
	if (ret && ret != DC_ERR_TIMEOUT)
		throw BackendException("DC_processMasterEvents() returned failure");
}
