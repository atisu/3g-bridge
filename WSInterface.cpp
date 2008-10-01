#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <uuid/uuid.h>

#include "Job.h"
#include "Logging.h"
#include "WSInterface.h"


using namespace std;


void *start_wsbinary(void *data);


WSInterface::WSInterface(GKeyFile *config)
{
	if (FALSE == g_key_file_has_group(config, "service"))
	{
		port = 0;
		return;
	}

	port = g_key_file_get_value(config, "service", "port", NULL);
	workdir = g_key_file_get_value(config, "service", "workdir", NULL);
	wsbinary = g_key_file_get_value(config, "service", "wsbinary", NULL);
}


WSInterface::~WSInterface()
{
}


void WSInterface::run()
{
	if (!wsbinary || !port || !workdir)
		return;

	char *cmdline[] = {wsbinary, port, workdir};
	pthread_t process_thread;
	pthread_create(&process_thread, NULL, (void*(*)(void*))start_wsbinary, (void*)cmdline);
	LOG(LOG_DEBUG, "Started web service interface binary.");
}


void WSInterface::shutdown()
{
	if (child)
		kill(child, SIGTERM);
}


void *start_wsbinary(void *data)
{
	char **cmdline = (char **)data;
	execl(cmdline[0], cmdline[0], cmdline[1], cmdline[2]);
	return NULL;
}


/*
void *ws_loop(void *data)
{
	SOAP_SOCKET cs;
	struct soap soap;
	pthread_t process_thread;

	soap_init(&soap);
        soap.send_timeout = 60;
        soap.recv_timeout = 60;
        soap.accept_timeout = 3600;
        soap.max_keep_alive = 100;

	SOAP_SOCKET ss;
	ss = soap_bind(&soap, NULL, 8080, 100);
        if (!soap_valid_socket(ss))
        {
                soap_print_fault(&soap, stderr);
                exit(-1);
        }

	LOG(LOG_DEBUG, "WSInterface: attached to port %d", 8080);

        for (;;)
        {
                //cs = soap_accept((struct soap *)data);
		cs = soap_accept(&soap);
		LOG(LOG_DEBUG, "WSInterface: processing incoming request");
		if (!cs)
		{
            		//soap_print_fault((struct soap *)data, stderr);
			soap_print_fault(&soap, stderr);
            		exit(-1);
		}
                //struct soap *tsoap = soap_copy((struct soap *)data);
		//struct soap *tsoap = soap_copy(&soap);
    		soap_serve(&soap);
                //pthread_create(&process_thread, NULL, (void*(*)(void*))process_request, (void*)tsoap);
	}
	//soap_done((struct soap *)data);
	soap_done(&soap);
	return NULL;
}


void *process_request(void *soap)
{
	LOG(LOG_DEBUG, "WSInterface: in a new thread...");
        pthread_detach(pthread_self());
        soap_serve((struct soap*)soap);
	soap_destroy((struct soap*)soap);
        soap_end((struct soap*)soap);
        soap_free((struct soap*)soap);
        return NULL;
}


int ns__addJob(struct soap *soap, struct ns__Job job, string &jobid)
{
	uuid_t id;
	char sid[37];
	uuid_generate(id);
	uuid_unparse(id, sid);

	cout << "OK" << endl;
	LOG(LOG_DEBUG, "WSInterface: in a new thread for handling addJob...");
	Job bJob((const char *)sid, job.alg.c_str(), job.grid.c_str(), job.args.c_str(), Job::INIT);
	LOG(LOG_DEBUG, "created a new job");

	vector<string> fsyspaths;
	struct soap_multipart *attachment;
	for (attachment = soap->mime.list; attachment; attachment = attachment->next)
	{
		if (!(*attachment).size)
			continue;
		const char *localname = (*attachment).description;
		if (!localname)
			continue;
		string fsyspath = string(workdir) + "/" + string(sid) + "." + localname;
		fsyspaths.push_back(fsyspath);
		int ofile = open(fsyspath.c_str(), O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
		if (-1 == ofile)
			continue;
		if (-1 == write(ofile, (void*)(*attachment).ptr, (*attachment).size))
		{
			close(ofile);
			unlink(fsyspath.c_str());
			continue;
		}
		close(ofile);
		bJob.addInput(string(localname), fsyspath);
	}

	bool allinputok = true;
	for (int i = 0; i < job.inputs.__size && allinputok; i++)
	{
		struct stat sstat;
		string fsyspath = string(workdir) + "/" + string(job.inputs.__ptr[i]);
		allinputok |= stat(fsyspath.c_str(), &sstat);
		unlink(fsyspath.c_str());
	}

	for (int i = 0; i < job.outputs.__size && allinputok; i++)
	{
		string fsyspath = string(workdir) + "/" + string(job.outputs.__ptr[i]);
		bJob.addOutput(job.outputs.__ptr[i], fsyspath);
	}

	if (!allinputok)
		jobid = "UNKNOWN";
	else
		jobid = string(sid);

	DBHandler *dbh = DBHandler::get();
	dbh->addJob(bJob);
	DBHandler::put(dbh);

	return SOAP_OK;
}


int ns__getStatus(struct soap *soap, string jobid, string &status)
{
	DBHandler *dbh = DBHandler::get();
	Job job("UNKNOWN", "UNKNOWN", "UNKNOWN", "", Job::INIT);
	dbh->getJob(job, jobid);
	DBHandler::put(dbh);
	status = string(statToStr(job.getStatus()));
	return SOAP_OK;
}


int ns__cancel(struct soap *soap, string jobid, ns__cancelResponse *out)
{
	DBHandler *dbh = DBHandler::get();
	dbh->updateJobStat(jobid, Job::CANCEL);
	DBHandler::put(dbh);
	return SOAP_OK;
}


int ns__getOutput(struct soap *soap, string jobid, struct ns__getOutputResponse *out)
{
	DBHandler *dbh = DBHandler::get();
	Job job("UNKNOWN", "UNKNOWN", "UNKNOWN", "", Job::INIT);
	dbh->getJob(job, jobid);
	DBHandler::put(dbh);

	vector<string> outs = job.getOutputs();
	char **fdatas = new char *[outs.size()];
	int i = 0;
	for (vector<string>::iterator it = outs.begin(); it != outs.end(); it++, i++)
	{
                struct stat sstat;
                if (-1 == stat(job.getOutputPath(*it).c_str(), &sstat))
                        perror("stat failed: ");
                fdatas[i] = new char[sstat.st_size];
                if (!fdatas[i])
                {
                        cerr << "Failed to allocate file data storage!" << endl;
                        return -1;
                }
                int ff = open(job.getOutputPath(*it).c_str(), O_RDONLY);
                if (-1 == ff)
                        perror("open failed: ");
                if (-1 == read(ff, fdatas[i], sstat.st_size))
                        perror("read failed: ");
                close(ff);
                cout << "Read file \"" << job.getOutputPath(*it) << "\" with size " << sstat.st_size << "." << endl;
                soap_set_mime_attachment(soap, fdatas[i], sstat.st_size, SOAP_MIME_BINARY, "application/octet-stream", NULL, NULL, it->c_str());
	}

	return SOAP_OK;
}
*/
