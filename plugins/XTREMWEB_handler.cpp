#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "DBHandler.h"
#include "XTREMWEB_handler.h"
#include "GridHandler.h"
#include "Job.h"
#include "Util.h"
#include "Conf.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <uuid/uuid.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <cstdlib>

using namespace std;

//#define FAKESUBMIT

//XWFIX add an additional call to xwclean
//this is a workaround to some bug in XtremWeb
#define XWFIX

//string * invoke_cmd(const char *exe,   char * const argv[]);

#define XWSUBMIT 0
#define XWSTATUS 1
#define XWDATA   2
#define XWGET    3
#define XWCLEAN  4


struct xwcommand
{
    int type;
    string bin;
    string args;
    string env;
    string xwid;
    string path;
    string name;
};


string * xtremwebClient(struct xwcommand xwc);
string g_cmdbase;//cmdbase for execution the commands
string g_env;//a global env parameter
string g_uuid; //uuid for Bridge jobs
string g_outputpath;//outputpath foroutput files
string g_timefordl; //time in seconds to download results for bridgss
string g_installdir; //the directory where you have installed 3g bridge


XWHandler::XWHandler(GKeyFile *config, const char *instance) throw (BackendException *)
{
    LOG(LOG_INFO, "XW Plugin created");

    name = instance;
    groupByNames = false;
    xwclient = g_key_file_get_string(config,instance,"xwclient-dir",NULL);

    g_env= g_key_file_get_string(config,instance,"xwenv",NULL);

    g_outputpath=g_key_file_get_string(config,instance,"output-dir",NULL);
    LOG(LOG_DEBUG,"XW bridge output dir:%s \n",g_outputpath.c_str());

    g_timefordl=g_key_file_get_string(config,instance,"xwdltime",NULL);
    LOG(LOG_DEBUG,"XW bridge download results time:%s \n",g_timefordl.c_str());

    g_installdir=g_key_file_get_string(config,instance,"installdir",NULL);
    LOG(LOG_DEBUG,"XW bridge installed dirs:%s \n",g_installdir.c_str());

    if (!xwclient)
        throw new BackendException("XtremWeb: no XtremWeb client path "
                                   "specified for %s", instance);
    cmdbase = string(xwclient) + "/bin/";
    g_cmdbase=cmdbase;

}


XWHandler::~XWHandler()
{
    /*
      char cmd[PATH_MAX];
      snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir.c_str());
      system(cmd);
    */
}


void XWHandler::submitJobs(JobVector &jobs) throw (BackendException *)
{
    if (!jobs.size())
        return;

    LOG(LOG_INFO, "XW Plugin (%s): about to submit %zd jobs.", name.c_str(), jobs.size());
    unsigned i = 0;
    for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++, i++)
    {

        Job *actJ = *it;

        string submit = cmdbase + "xwsubmit";

        //Separate between application name and args
        string args = actJ->getArgs();
        string bin  = actJ->getName();

        string env = "";
        string filesToZip = "";
        string zipFile =  "/tmp/" + actJ->getId() + ".zip";
        auto_ptr< vector<string> > ins = actJ->getInputs();
        for (unsigned j = 0; j < ins->size(); j++)
        {

            string fileName= (*ins)[j];
            string path= actJ->getInputPath((*ins)[j]).c_str();
            LOG(LOG_INFO," DATA[%s|%s]DATA\n",path.c_str(),fileName.c_str());
            filesToZip += path;
        }

        string zip = "zip -j " + zipFile + " " + filesToZip;

        string cmd="/usr/bin/"+zip;
        LOG(LOG_INFO,"zipping... \n%s",cmd.c_str());
        system(cmd.c_str());

        LOG(LOG_INFO,"%s zipped \n",zipFile.c_str());

        env = "--xwenv " + zipFile;
        g_env=env;
        submit = submit + " " + bin + " " + args + " " + env;
        //rm zipFile
        struct xwcommand xwsubmit;
        xwsubmit.type=XWSUBMIT;
        xwsubmit.bin=bin;
        xwsubmit.args=args;
        xwsubmit.env = env;
#ifndef FAKESUBMIT
        string * retour = xtremwebClient(xwsubmit);

#endif
        LOG(LOG_DEBUG, "XW Plugin: job submitted, now detemining node IDs.");

        // then parse out the result
        // get the XWID

        int pos_xwid = retour->rfind("xw://");
        string xwid = retour->substr(pos_xwid,retour->length()-1);
        //trim xwid
        size_t endpos = xwid.find_last_not_of(" \t\n");
        if ( string::npos != endpos )
            xwid = xwid.substr( 0, endpos+1 );
        //insert XWID in the database
        actJ->setGridId(xwid);

        //TODO: put the file
        //TODO: create the xwenv files

        //change the status of the job
        actJ->setStatus(Job::RUNNING);
    }
    LOG(LOG_INFO, "XW Plugin: job submission finished.");
}


void XWHandler::updateStatus(void) throw (BackendException *)
{
    LOG(LOG_INFO, "XW Plugin (%s): about to update status of jobs.", name.c_str());

//	createCFG();

    DBHandler *jobDB = DBHandler::get();
    jobDB->pollJobs(this, Job::RUNNING, Job::CANCEL);
    DBHandler::put(jobDB);

    LOG(LOG_INFO, "XW Plugin: status update finished.");
}


void XWHandler::updateJob(Job *job, string status)
{
    const struct
    {
        string XWDGs;
        Job::JobStatus jobS;
    } statusRelation[] =
    {
        {"WAITING", Job::RUNNING},
        {"PENDING", Job::RUNNING},
        {"RUNNING", Job::RUNNING},
        {"DATAREQUEST", Job::RUNNING},
        {"AVAILABLE", Job::RUNNING},
        {"RUNNING", Job::RUNNING},
        {"COMPLETED", Job::FINISHED},
        {"ERROR", Job::ERROR},
        {"ABORTED", Job::ERROR},
        {"LOST", Job::ERROR},
        {"UNAVAILABLE", Job::ERROR},
        {"", Job::INIT},
    };

    string statStr=status;

    LOG(LOG_INFO,"\nStatus Str:%s\n",statStr.c_str());
    LOG(LOG_INFO, "XWDG Plugin (%s): updating status of job \"%s\" (unique identifier is \"%s\").", name.c_str(), job->getGridId().c_str(), job->getId().c_str());

    for (unsigned j = 0; statusRelation[j].XWDGs != ""; j++)
    {

        if (statusRelation[j].XWDGs == statStr)
        {
// Todo to download resultats when status is "completed".

            job->setStatus(statusRelation[j].jobS);
            break;
        }
    }
}


void XWHandler::poll(Job *job) throw (BackendException *)
{
    string xwid="";
    xwid = job->getGridId();
    g_uuid = job->getId();

    xwcommand xwstatus;
    switch (job->getStatus())
    {
	case Job::RUNNING:
	{
    	    xwstatus.xwid=xwid;
    	    //we need this to fix an XtremWeb bug
    	    string * retour;
#ifdef XWFIX
    	    xwstatus.type=XWCLEAN;
    	    retour = xtremwebClient(xwstatus);
#endif
    	    xwstatus.type=XWSTATUS;
    	    retour = xtremwebClient(xwstatus);

    	    string status = "ERROR";
    	    size_t pos_xwid = retour->rfind("status=\"");
    	    if (string::npos != pos_xwid )
    	    {

        	status = retour->substr(pos_xwid+8,retour->length()-1);
        	//trim xwid
        	size_t endpos = status.find("\"");
        	if ( string::npos != endpos )
            	    status = status.substr( 0, endpos );
    	    }

    	    LOG(LOG_INFO, "STATUS[ %s ]STATUS",status.c_str());

    	    if (status=="COMPLETED")
    	    {
        	xwstatus.xwid=xwid;
        	//we need this to fix an XtremWeb bug

        	xwstatus.type=XWGET;
        	retour = xtremwebClient(xwstatus);
    	    }
    	    updateJob(job,status);

    	    break;
	}

	case Job::FINISHED:
	{

    	    xwstatus.xwid=xwid;

    	    string * retour;

    	    LOG(LOG_INFO,"POLL : %s ,FINISHED\n",xwid.c_str());
    	    xwstatus.type=XWGET;
    	    retour = xtremwebClient(xwstatus);
    	    break;
	}
    }

    LOG(LOG_INFO,"POLL : %s \n",xwid.c_str());
}


string * xtremwebClient(struct xwcommand xwc)
{
    int pipe_stdout[2], pipe_stderr[2], c;
    char buffer[1024];
    string * message = new string("");

    /* creation pipes de communication */
    pipe(pipe_stdout);
    pipe(pipe_stderr);

    switch (xwc.type)
    {
	case XWSUBMIT:
	{
    	    string submitpath="";
    	    submitpath=g_cmdbase + "xwsubmit";

    	    char t[512];
    	    memset(t,0,512);
    	    strcpy(t,submitpath.c_str());

    	    LOG(LOG_DEBUG,"t[]=%s\n",t);
    	    char *  args[] = {  "bash",t,"",  (char*)NULL };
    	    char tmp[512];
    	    string submit = xwc.bin + " " + xwc.args + " " + xwc.env;
    	    args[2] = strcpy(tmp, submit.c_str());

    	    LOG(LOG_INFO,"xwclient : %s %s %s\n", args[0], args[1], args[2]);

    	    if (fork()==0)
    	    {
        	close (pipe_stdout[0]);
        	close (pipe_stderr[0]);
        	/* redirect to pipe */
        	dup2(pipe_stdout[1], STDOUT_FILENO);
        	dup2(pipe_stderr[1], STDERR_FILENO);
        	execv("/bin/bash",args);
    	    }
    	    break;
	}

	case XWSTATUS:
        {
    	    string submitpath="";
    	    submitpath=g_cmdbase + "xwstatus";

    	    char t[512];
    	    memset(t,0,512);
    	    strcpy(t,submitpath.c_str());

    	    LOG(LOG_DEBUG,"t[]=%s\n",t);
    	    char *  args[] = {  "bash",t,"",  (char*)NULL };
    	    char tmp[512];
    	    args[2] = strcpy(tmp, xwc.xwid.c_str());

    	    LOG(LOG_INFO,"xwclient : %s %s %s\n", args[0], args[1], args[2]);

    	    if (fork()==0)
    	    {
        	close (pipe_stdout[0]);
        	close (pipe_stderr[0]);
        	/* redirect to pipe */
        	dup2(pipe_stdout[1], STDOUT_FILENO);
        	dup2(pipe_stderr[1], STDERR_FILENO);
        	execv("/bin/bash",args);
    	    }
    	    break;
	}

	case XWDATA:
	{
	    string submitpath="";
    	    submitpath=g_cmdbase + "xwdata";

    	    char t[512];
    	    memset(t,0,512);
    	    strcpy(t,submitpath.c_str());

    	    LOG(LOG_DEBUG,"t[]=%s\n",t);
    	    char *  args[] = {  "bash",t,"",  (char*)NULL };

    	    char tmp[512];
    	    string data = xwc.name + " " + xwc.path;
    	    args[2] = strcpy(tmp, data.c_str());

    	    LOG(LOG_INFO,"xwclient : %s %s %s\n", args[0], args[1], args[2]);

    	    if (fork()==0)
    	    {
        	close (pipe_stdout[0]);
    		close (pipe_stderr[0]);
        	/* redirect to pipe */
        	dup2(pipe_stdout[1], STDOUT_FILENO);
        	dup2(pipe_stderr[1], STDERR_FILENO);
        	execv("/bin/bash",args);
    	    }
    	    break;
	}

	case XWCLEAN:
	{
    	    string submitpath="";
    	    submitpath=g_cmdbase + "xwclean";

    	    char t[512];
    	    memset(t,0,512);
    	    strcpy(t,submitpath.c_str());
    	    LOG(LOG_DEBUG,"xwclean t[]=%s\n",t);
    	    char *  args[] = {  "bash",t,"",  (char*)NULL };
    	    char tmp[512];
    	    args[2] = strcpy(tmp, xwc.xwid.c_str());
    	    LOG(LOG_DEBUG,"xwclient : %s %s %s\n", args[0], args[1], args[2]);

    	    if (fork()==0)
    	    {
        	close (pipe_stdout[0]);
        	close (pipe_stderr[0]);
        	/* redirect to pipe */
        	dup2(pipe_stdout[1], STDOUT_FILENO);
        	dup2(pipe_stderr[1], STDERR_FILENO);
        	execv("/bin/bash",args);
    	    }
    	    break;
	}

	case XWGET:
	{
    	    system("pwd");
    	    string workdir=g_installdir;
    	    chdir(workdir.c_str());

    	    LOG(LOG_DEBUG,"chdir : %s \n", workdir.c_str());

    	    string submitpath="";
    	    submitpath=g_cmdbase + "xwdownload";

    	    char t[512];
    	    memset(t,0,512);
    	    strcpy(t,submitpath.c_str());
    	    LOG(LOG_DEBUG,"t[]=%s\n",t);
    	    char *  args[] = {"bash",t,"",  (char*)NULL };
    	    char tmp[512];
    	    string submit = xwc.bin + " " + xwc.args + " " + xwc.xwid+" "+xwc.env;
    	    args[2] = strcpy(tmp, submit.c_str());
    	    LOG(LOG_DEBUG,"xwclient : %s %s %s\n", args[0], args[1], args[2]);

    	    if (fork()==0)
    	    {
        	close (pipe_stdout[0]);
        	close (pipe_stderr[0]);
        	/* redirect to pipe */
        	dup2(pipe_stdout[1], STDOUT_FILENO);
        	dup2(pipe_stderr[1], STDERR_FILENO);
        	execv("/bin/bash",args);
    	    }
    	    int timedl=atoi(g_timefordl.c_str());
    	    LOG(LOG_DEBUG,"time to dl: %d \n",timedl);
    	    sleep(timedl);//waiting for results file downloaded
    	    string uuid=g_uuid;

	    int xwidlen=(xwc.xwid).length();
	    int idlen=36;
	    //this xwgridid is generated, the length is fixed to 36
	    int envlen=xwidlen-idlen;
	    LOG(LOG_DEBUG,"envlen,idlen: %d %d \n",envlen,idlen);

	    string xwid=xwc.xwid.substr(envlen,idlen);
    	    string fileToUnzip= "*_ResultsOf_"+xwid+".zip";

    	    string path=g_outputpath+"/"+uuid.substr(0,2)+"/"+uuid+"/";

    	    LOG(LOG_DEBUG,"outputpath : %s \n", path.c_str());

    	    string cmdmv="mv "+fileToUnzip+" "+path;

    	    string unzip = "unzip -o " + path+fileToUnzip;
    	    string cmdunzip="/usr/bin/"+unzip+" -d "+path;

    	    LOG(LOG_DEBUG,"cmdmv : %s \n", cmdmv.c_str());

    	    system(cmdmv.c_str());

    	    LOG(LOG_DEBUG,"cmdunzip : %s \n", cmdunzip.c_str());
    	    system(cmdunzip.c_str());

	    break;
	}

    }

    (void)wait(NULL);
    close (pipe_stdout[1]);
    close (pipe_stderr[1]);
    memset(buffer, 0, 1024);

    /* Lecture pipes */
    while ((c=read(pipe_stdout[0], buffer, 1024))>0)
    {
        message->append(buffer);
        memset(buffer, 0, 1024);
    }

    return message;
}

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new XWHandler(config, instance);
}
