#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <vector>
#include "CGJob.h"
#include "DCAPIHandler.h"

using namespace std;


DCAPIHandler::DCAPIHandler(const string conf, const string basedir):bdir(basedir)
{
  if (DC_OK != DC_initMaster(conf.c_str()))
    throw DC_initMasterError;
}


DCAPIHandler::~DCAPIHandler()
{
}


void DCAPIHandler::submitJobs(vector<CGJob *> *jobs)
{
  // TODO: batching
  for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++) {
    DC_Workunit *wu;
    char *tag = new char[37];
    CGJob *job = *it;
    
    vector<string> inputs = job->getInputs();
    vector<string> outputs = job->getOutputs();
    string localname, inputpath;
    CGAlg *type = job->getType();
    string algName = type->getName();
    
    // Add job id to wu_tag
    uuid_unparse(*id, tag);
    
    // Get command line parameters and create null-terminated list
    list<string> *arglist = job->getArgv();
    int i = 0, size = arglist->size() + 1;
    const char *argv[size];
    for (list<string>::iterator it = arglist->begin(); it != arglist->end(); it++) {
      int arglength = it->length();
      const char *argtoken = new char[arglength];
      argtoken = it->c_str();
      argv[i] = argtoken;
      i++;
    }
    argv[i] = NULL;
    
    // Create WU descriptor
    wu = DC_createWU(algName.c_str(), argv, 0, tag);
    // Free allocated c strings
    delete [] tag;
    for (i = size - 1; i = 0; i--) {
      delete [] argv[i];
    }
    
    if (!wu) {
      throw DC_createWUError;
    }
    
    // Register WU inputs
    for (vector<string>::iterator it = inputs.begin(); it != inputs.end(); it++) {
      inputpath = job->getInputPath(localname = *it);
      
      if (DC_addWUInput(wu, localname.c_str(), inputpath.c_str(), DC_FILE_PERSISTENT)) {
	throw DC_addWUInputError;
      }
    }
    
    //Register WU outputs
    for (vector<string>::iterator it = outputs.begin(); it != outputs.end(); it++) {
      localname = *it;
      // Do not register stdout and stderr as inputs
      if (localname.compare("stdout.txt") && localname.compare("stderr.txt"))
	if (DC_addWUOutput(wu, localname.c_str()))
	  throw DC_addWUOutputError;
    }
    
    // Submit WU
    if (DC_submitWU(wu)) {
      throw DC_submitWUError;
    }
    
    // Serialize WU and set the wuID of the job entity
    job->setGridId(DC_serializeWU(wu));
    
    // Set status of job to CG_RUNNING
    job->setStatus(CG_RUNNING);
  }
}


void DCAPIHandler::getStatus(vector<CGJob *> *jobs)
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
    if (event->type != DC_MASTER_RESULT) {
      DC_destroyMasterEvent(event);
      continue;
    }
    // Now get the WU tag of the event
    wutag = DC_getWUTag(event->wu);
    if (uuid_parse(wutag, *id) == -1) {
      cerr << "Failed to parse uuid" << endl;
      DC_destroyWU(event->wu);
      DC_destroyMasterEvent(event);
      continue;
    }
    // Search for the id in the received jobs
    for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++) {
      CGJob *job = *it;
      if (job->getGridID() == (string)wutag) {
	if (!event->result)
	  job->setStatus(CG_ERROR);
	else
	  job->setStatus(CG_FINISHED);
	DC_destroyMasterEvent(event);
      }
    }
  }
}


void DCAPIHandler::getOutputs(vector<CGJob *> *jobs)
{
  for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++) {
    CGJob *job = *it;
    string workdir = bdir + "/workdir/" + job->getName() + "/" + job->getGridID() + "/";
  }
}


void DCAPIHandler::cancelJobs(vector <CGJob *> *jobs)
{
  // TODO: check for batched jobs, and do not cancel them as along as every
  // instance has been marked for removal
  for (vector<CGJob *>::iterator it = jobs->begin(); it != jobs->end(); it++) {
    CGJob *job = *it;
    if (job->getStatus() == CG_RUNNING) {
      DC_Workunit *wu = DC_deserializeWU(job->getGridId().c_str());
      DC_cancelWU(wu);
    }
    job->setStatus(CG_ERROR);
  }
}
