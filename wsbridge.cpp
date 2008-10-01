#include <string>
#include <stdlib.h>
#include <pthread.h>
#include <uuid/uuid.h>

#include "Job.h"
#include "DBHandler.h"
#include "soap/soapH.h"
#include "soap/gggbridge.nsmap"


using namespace std;


static char *workdir;
void *process_request(void *soap);
GKeyFile *global_config = NULL;


int main(int argc, char **argv)
{
	int port;
        struct soap soap;
        pthread_t process_thread;

	if (argc != 2)
	{
		cerr << "Usage: " << argv[0] << " <configfile>" << endl;
		exit(-1);
	}

	GError *error = NULL;
	global_config = g_key_file_new();
	g_key_file_load_from_file(global_config, argv[1], G_KEY_FILE_NONE, &error);
	if (error)
	{
		cerr << "Failed to load the config file: " << error->message << endl;
	        exit(1);
	}
	port = g_key_file_get_integer(global_config, "service", "port", &error);
	if (!port || error)
	{
		cerr << "Failed to parse port: " << error->message << endl;
	        exit(1);
	}
	if (port < 0)
	{
		cerr << "Invalid port number specified, please use unsigned integers" << endl;
		exit(-1);
	}
	workdir = g_key_file_get_string(global_config, "service", "workdir", &error);
	if (!workdir || error)
	{
		cerr << "Failed to get working directory: " << error->message << endl;
	        exit(1);
	}

	soap_init(&soap);
        soap.send_timeout = 60;
	soap.recv_timeout = 60;
	soap.accept_timeout = 3600;
	soap.max_keep_alive = 100;

	SOAP_SOCKET ss = soap_bind(&soap, NULL, 8080, 100);
	if (!soap_valid_socket(ss))
	{
		soap_print_fault(&soap, stderr);
		exit(-1);
	}

        for (;;)
        {
    		SOAP_SOCKET cs = soap_accept(&soap);
                if (!cs)
                {
                        soap_print_fault(&soap, stderr);
                        exit(-1);
		}
		cout << "New connection!" << endl;
                struct soap *tsoap = soap_copy(&soap);
		pthread_create(&process_thread, NULL, (void*(*)(void*))process_request, (void*)tsoap);
	}
	soap_done(&soap);
	return 0;
}


void *process_request(void *soap)
{
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

	cout << "Serving ns__addJob..." << endl;
	cout << " Job alg:  " << job.alg << endl;
	cout << " Job grid: " << job.grid << endl;
	cout << " Job args: " << job.args << endl;
	cout << " Job ins:  " << job.inputs->__size << endl;
	if (job.outputs)
		cout << " Job outs: " << job.outputs->__size << endl;
	for (int i = 0; i < job.inputs->__size; i++)
		cout << " Job in:   " << job.inputs->__ptr[i] << endl;
	if (job.outputs)
		for (int i = 0; i < job.outputs->__size; i++)
			cout << " Job out:  " << job.outputs->__ptr[i] << endl;
        Job bJob((const char *)sid, job.alg.c_str(), job.grid.c_str(), job.args.c_str(), Job::INIT);

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
        for (int i = 0; i < job.inputs->__size && allinputok; i++)
        {
                struct stat sstat;
                string fsyspath = string(workdir) + "/" + string(sid) + "." + string(job.inputs->__ptr[i]);
                allinputok |= stat(fsyspath.c_str(), &sstat);
                unlink(fsyspath.c_str());
        }

	if (job.outputs)
	        for (int i = 0; i < job.outputs->__size && allinputok; i++)
    		{
            		string fsyspath = string(workdir) + "/" + string(job.outputs->__ptr[i]);
            		bJob.addOutput(job.outputs->__ptr[i], fsyspath);
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
	Job tJob;
	dbh->getJob(tJob, jobid);
	status = string(statToStr(tJob.getStatus()));
	DBHandler::put(dbh);
	return SOAP_OK;
}


int ns__cancel(struct soap *soap, string jobid, ns__cancelResponse *out)
{
        DBHandler *dbh = DBHandler::get();
	dbh->deleteJob(jobid);
	DBHandler::put(dbh);
	return SOAP_OK;
}


int ns__getOutput(struct soap *soap, string jobid, struct ns__getOutputResponse *out)
{
	return SOAP_OK;
}
