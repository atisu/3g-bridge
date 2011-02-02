/*
 * Copyright (C) 2011      LAL, Univ Paris-Sud, IN2P3/CNRS  (Etienne URBAH)
 * Copyright (C) 2008-2010 MTA SZTAKI LPDS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * In addition, as a special exception, MTA SZTAKI gives you permission to
 * link the code of its release of the 3G Bridge with the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the same
 * license as the "OpenSSL" library), and distribute the linked executables.
 * You must obey the GNU General Public License in all respects for all of the
 * code used other than "OpenSSL". If you modify this file, you may extend
 * this exception to your version of the file, but you are not obligated to do
 * so. If you do not wish to do so, delete this exception statement from your
 * version.
 */

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

//XW_CLEAN_FORCED adds an preparation call to xwclean
//This is a workaround to some bug in XtremWeb
//#define XW_CLEAN_FORCED

//#define XW_SLEEP_BEFORE_DOWNLOAD

//OBSOLETE  string * invoke_cmd(const char * exe,   char * const argv[]);

enum xw_command_enumeration
{
  XW_CLEAN,
  XW_DATA,
  XW_DOWNLOAD,
  XW_RESULT,
  XW_RM,
  XW_STATUS,
  XW_SUBMIT
};

struct xw_command_t
{
  int    xwcommand;
  string appname;
  string appargs;
  string appenv;
  string xwjobid;
  string bridgejobid;
  string path;
  string name;
};

struct xw_returned_t
{
  int    retcode;
  string message;
};


//============================================================================
//  Global variables
//============================================================================
string g_xw_client_bin_folder;        // XtremWeb-HEP client binaries folder
string g_xw_files_folder;             // Folder for XW input and output files
string g_bridge_output_folder;        // Folder for bridge output files
int    g_sleep_time_before_download;  // Sleeping time before download in s


//============================================================================
//  Constructor
//============================================================================
XWHandler::XWHandler(GKeyFile * config, const char * instance)
  throw (BackendException *): GridHandler(config, instance)
{
  const char * function_name = "XWHandler::XWHandler";
  
  struct stat file_status;
  
  LOG(LOG_INFO, "%s(%s)  Plugin creation for instance   '%s'",
                function_name, name.c_str(), instance);
  
  groupByNames = false;
  
  //--------------------------------------------------------------------------
  // xwclient_install_dir
  //--------------------------------------------------------------------------
  char * xw_client_folder = g_key_file_get_string(config, instance,
                                                "xwclient_install_dir", NULL);
  if ( ! xw_client_folder )
      throw new BackendException("XWHandler::XWHandler  No XtremWeb client "
                                 "folder specified for '%s'", instance);
  g_strstrip(xw_client_folder);
  LOG(LOG_INFO, "%s(%s)  XW client install folder:      '%s'",
                function_name, name.c_str(), xw_client_folder);
  g_xw_client_bin_folder = string(xw_client_folder) + "/bin/";
  LOG(LOG_INFO, "%s(%s)  XW client binaries folder:     '%s'",
                function_name, name.c_str(), g_xw_client_bin_folder.c_str());
  
  if ( (stat(g_xw_client_bin_folder.c_str(), &file_status) != 0) ||
       ((file_status.st_mode & S_IFDIR) == 0) ||
       ((file_status.st_mode & S_IROTH) == 0) )
      throw new BackendException("XWHandler::XWHandler  '%s' is NOT a "
                                 "folder readable by group",
                                 g_xw_client_bin_folder.c_str());
  
  //--------------------------------------------------------------------------
  // xw_files_dir
  //--------------------------------------------------------------------------
  g_xw_files_folder = g_key_file_get_string(config, instance, "xw_files_dir",
                                            NULL);
  LOG(LOG_INFO, "%s(%s)  Folder for XtremWeb files:     '%s'",
                function_name, name.c_str(), g_xw_files_folder.c_str());
  
  if ( (stat(g_xw_files_folder.c_str(), &file_status) != 0) ||
       ((file_status.st_mode & S_IFDIR) == 0) ||
       ((file_status.st_mode & S_IWGRP) == 0) )
      throw new BackendException("XWHandler::XWHandler  '%s' is NOT a "
                                 "folder writable by group",
                                 g_xw_files_folder.c_str());
  
  //--------------------------------------------------------------------------
  // wssubmitter-output-dir
  //--------------------------------------------------------------------------
  g_bridge_output_folder = g_key_file_get_string(config, "wssubmitter",
                                                 "output-dir", NULL);
  LOG(LOG_INFO, "%s(%s)  3G Bridge output folder:       '%s'",
                function_name, name.c_str(), g_bridge_output_folder.c_str());
  
  if ( (stat(g_bridge_output_folder.c_str(), &file_status) != 0) ||
       ((file_status.st_mode & S_IFDIR) == 0) ||
       ((file_status.st_mode & S_IWGRP) == 0) )
      throw new BackendException("XWHandler::XWHandler  '%s' is NOT a "
                                 "folder writable by group",
                                 g_bridge_output_folder.c_str());
  
#ifdef XW_SLEEP_BEFORE_DOWNLOAD
  //--------------------------------------------------------------------------
  // xw_sleep_time_before_download
  //--------------------------------------------------------------------------
  string sleep_time_before_download  = g_key_file_get_string(config, instance,
                                       "xw_sleep_time_before_download", NULL);
  LOG(LOG_INFO, "%s(%s)  Sleeping time before download: '%s'",
                function_name, name.c_str(),
                sleep_time_before_download.c_str());
  g_sleep_time_before_download = atoi(sleep_time_before_download.c_str());
#endif
}


//============================================================================
//  Destructor
//============================================================================
XWHandler::~XWHandler()
{
  const char * function_name = "XWHandler::~XWHandler";
  
  LOG(LOG_INFO, "%s(%s)  Successfully called for destruction",
                function_name, name.c_str());
}


//============================================================================
//  Function  xtremwebClient     
//  @param  xw_command  Struct containing the XtremWeb command type and params
//  @return             Message displayed by the command
//
//  This function executes the command given in parameter,
//  retrieves the message normally displayed by the command on STDOUT,
//  and returns a structure containing the return code and this message.
//============================================================================
xw_returned_t xtremwebClient(xw_command_t xw_command)
{
  const char * function_name = "XWHandler  xtremwebClient";
  string       name = "";
  
  string submitpath;
  string submitparams;
  
  LOG(LOG_DEBUG, "%s(%s)  Command type = %d",
                 function_name, name.c_str(), xw_command.xwcommand);
  
  //--------------------------------------------------------------------------
  // Depending on the XtremWeb command, build the adequate command and params
  //--------------------------------------------------------------------------
  switch ( xw_command.xwcommand )
  {
    case XW_CLEAN:
    {
      submitpath   = g_xw_client_bin_folder + "xwclean";
      submitparams = "";
      break;
    }
    
    case XW_DATA:
    {
      submitpath   = g_xw_client_bin_folder + "xwdata";
      submitparams = xw_command.name + " " + xw_command.path;
      break;
    }
    
    case XW_RM:
    {
      submitpath   = g_xw_client_bin_folder + "xwrm";
      submitparams = xw_command.xwjobid;
      break;
    }
    
    case XW_STATUS:
    {
      submitpath   = g_xw_client_bin_folder + "xwstatus";
      submitparams = xw_command.xwjobid;
      break;
    }
    
    case XW_SUBMIT:
    {
      submitpath   = g_xw_client_bin_folder + "xwsubmit";
      submitparams = xw_command.appname + " " + xw_command.appargs + " " +
                     xw_command.appenv;
      break;
    }
    
    case XW_DOWNLOAD:
    {
      submitpath   = g_xw_client_bin_folder + "xwdownload";
      submitparams = xw_command.appname + " " + xw_command.appargs + " " +
                     xw_command.xwjobid + " " + xw_command.appenv;
      
      string workdir = g_xw_files_folder;
      LOG(LOG_DEBUG, "%s(%s)  Executing chdir(%s)",
                     function_name, name.c_str(), workdir.c_str());
      chdir(workdir.c_str());
      
      break;
    }
    
    case XW_RESULT:
    {
      submitpath   = g_xw_client_bin_folder + "xwresults";
      submitparams = xw_command.xwjobid;
      
      char * cwd = getcwd((char*)NULL, 0);
      LOG(LOG_DEBUG, "%s(%s)  cwd = '%s'",
                     function_name, name.c_str(), cwd);
      
      if  ( strcmp(cwd, g_xw_files_folder.c_str()) != 0 )
      {
        string workdir = g_xw_files_folder;
        LOG(LOG_DEBUG, "%s(%s)  Executing chdir('%s')",
                       function_name, name.c_str(), workdir.c_str());
        chdir(workdir.c_str());
      }
      
      break;
    }
    
  }
  
  //--------------------------------------------------------------------------
  // Execute the adequate XtremWeb command, and retrieve the displayed message
  //--------------------------------------------------------------------------
  LOG(LOG_DEBUG, "%s(%s)  Command = '%s'  Params = '%s'",
                 function_name, name.c_str(), submitpath.c_str(),
                 submitparams.c_str());
  
  // Create path and arguments for the command to execute
  short  command_length = submitpath.length()   + 1;
  short  params_length  = submitparams.length() + 1;
  char * command        = new char[command_length];
  char * params         = new char[params_length];
  
  char   shell_path[] = "/bin/sh";
  char   shell[]      = "sh";
  char * args[]       = { shell,
                          strcpy(command, submitpath.c_str()),
                          strcpy(params,  submitparams.c_str()),
                          (char *)NULL };
  LOG(LOG_INFO, "%s(%s)  %s %s %s",
                function_name, name.c_str(), args[0], args[1], args[2]);
  
  // Create pipes permitting to retrieve STDOUT and STDERR of child process
  int   pipe_stdout[2];
  int   pipe_stderr[2];
  pid_t process_id;
  
  pipe(pipe_stdout);
  pipe(pipe_stderr);
  
  // Create child process to execute the command
  xw_returned_t returned_values;
  process_id = fork();
  
  if ( process_id < 0)
  {
    LOG(LOG_NOTICE, "%s(%s)    return_code = %d  for fork()",
                    function_name, name.c_str(), process_id);
    returned_values.retcode = process_id;
    returned_values.message = "FORK ERROR";
    return returned_values;
  }
  
  if ( process_id == 0)
  {
    LOG(LOG_DEBUG, "%s(%s)  Child process:  %s %s %s",
                   function_name, name.c_str(), args[0], args[1], args[2]);
    
    close(pipe_stdout[0]);
    close(pipe_stderr[0]);
    
    // Redirect STDOUT and STDERR to pipes
    dup2(pipe_stdout[1], STDOUT_FILENO);
    dup2(pipe_stderr[1], STDERR_FILENO);
    
    // Execute the above prepared command
    LOG(LOG_DEBUG, "%s(%s)  Child process:  execv(%s)",
                   function_name, name.c_str(), shell_path);
    execv(shell_path, args);
  }
  
  // Wait until child has finished
  //IF wait(&var) FAILS:  int * stat_loc = new int;
  (void)wait(&(returned_values.retcode));
  close(pipe_stdout[1]);
  close(pipe_stderr[1]);
  
  LOG(LOG_DEBUG, "%s(%s)  Return code = x'%X' --> %d",
                 function_name, name.c_str(), returned_values.retcode,
                 returned_values.retcode / 256);
  
  //IF wait(&var) FAILS:  delete stat_loc;
  delete params;
  delete command;
  
  // Read pipe to retrieve STDOUT of forked child
  int         count;
  const short buffer_size = 1024;
  char        buffer[buffer_size];
  
  memset(buffer, 0, buffer_size);
  while ( (count=read(pipe_stdout[0], buffer, buffer_size)) > 0 )
  {
    (returned_values.message).append(buffer);
    memset(buffer, 0, buffer_size);
  }
  LOG(LOG_DEBUG, "%s(%s)  Displayed message = '%s'",
                 function_name, name.c_str(),
                 (returned_values.message).c_str());
  
  return returned_values;
}


//============================================================================
//  Member function  submitJobs
//============================================================================
void XWHandler::submitJobs(JobVector &jobs) throw (BackendException *)
{
  const char * function_name = "XWHandler::submitJobs";
  
  LOG(LOG_INFO, "%s(%s)  %d job(s) have to be submitted to XtremWeb",
                function_name, name.c_str(), jobs.size());
  
  if ( jobs.size() < 1 )
      return;
  
  Job *         current_job;
  const char *  bridge_job_id;
  string        job_app_name;
  string        job_app_args;
  string        input_file_paths;
  string        zip_file_name;
  string        input_file_name;
  string        input_file_path;
  auto_ptr< vector<string> > input_files;
  string        zip_command;
  int           return_code;
  const char *  job_batch_id;
  xw_command_t  xw_submit;
  xw_returned_t returned_values;
  size_t        pos_xwid;
  string        xw_job_id;
  
  for ( JobVector::iterator job_iterator = jobs.begin();
        job_iterator != jobs.end();
        job_iterator++ )
  {
    current_job   = *job_iterator;
    bridge_job_id = current_job->getId().c_str();
    job_app_name  = current_job->getName();
    job_app_args  = current_job->getArgs();
    
    //------------------------------------------------------------------------
    // Retrieve list of input files, and store all of them inside a ZIP
    //------------------------------------------------------------------------
    input_file_paths = "";
    zip_file_name    = g_xw_files_folder + "/" + current_job->getId() +
                       ".zip";
    input_files      = current_job->getInputs();
    for ( unsigned file_number = 0;
          file_number < input_files->size();
          file_number++ )
    {
      input_file_name = (*input_files)[file_number];
      input_file_path = current_job->getInputPath(input_file_name);
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  input_file_name = '%s'  "
                     "input_file_path = '%s'",
                     function_name, name.c_str(), bridge_job_id,
                     input_file_name.c_str(), input_file_path.c_str());
      input_file_paths += " " + input_file_path;  // Insert blank as separator
    }
    
    string zip_command = "/usr/bin/zip -j " + zip_file_name + " " +
                         input_file_paths;
    LOG(LOG_DEBUG, "%s(%s)    Job '%s'  %s",
                   function_name, name.c_str(), bridge_job_id,
                   zip_command.c_str());
    
    return_code = system(zip_command.c_str());
    LOG(LOG_DEBUG, "%s(%s)    Job '%s'  return_code = %d  for zipping '%s'",
                   function_name, name.c_str(), bridge_job_id, return_code,
                   zip_file_name.c_str());
    
    if ( return_code != 0 )
    {
      LOG(LOG_ERR, "%s(%s)    Job '%s'  return_code = %d  for zipping '%s'",
                   function_name, name.c_str(), bridge_job_id, return_code,
                   zip_file_name.c_str());
      current_job->setStatus(Job::ERROR);
      return;
    }
    
    //------------------------------------------------------------------------
    // If '_3G_BRIDGE_BATCH_ID' exists among the environment variables of the
    // bridge job, map it to the 'group_id' attribute of the XtremWeb job
    //------------------------------------------------------------------------
    job_batch_id = current_job->getEnv("_3G_BRIDGE_BATCH_ID").c_str();
    if ( strlen(job_batch_id) > 0 )
    {
      LOG(LOG_DEBUG, "%s(%s)    Job '%s'  Job batch id = '%s'",
                     function_name, name.c_str(), bridge_job_id,
                     job_batch_id);
      //TODO:  Map it to the 'group_id' attribute of the XtremWeb job
    }
    
    //------------------------------------------------------------------------
    // Submit the job to XtremWeb
    //------------------------------------------------------------------------
    xw_submit.xwcommand = XW_SUBMIT;
    xw_submit.appname   = job_app_name;
    xw_submit.appargs   = job_app_args;
    xw_submit.appenv    = " --xwenv " + zip_file_name;
    returned_values     = xtremwebClient(xw_submit);
    
    LOG(LOG_DEBUG,"%s(%s)    Job '%s' submitted  XtremWeb displayed message = '%s'",
                  function_name, name.c_str(), bridge_job_id,
                  (returned_values.message).c_str());
    
    if ( returned_values.retcode != 0 )
    {
      LOG(LOG_NOTICE, "%s(%s)    Job '%s'  return_code = x'%X' --> %d  for "
                      "XtremWeb submission",
                      function_name, name.c_str(), bridge_job_id,
                      returned_values.retcode, returned_values.retcode / 256);
      LOG(LOG_NOTICE, "%s(%s)    Job '%s'  NOT accepted  '%s'",
                      function_name, name.c_str(), bridge_job_id,
                      (returned_values.message).c_str());
      current_job->setStatus(Job::ERROR);
      return;
    }
    
    //------------------------------------------------------------------------
    // From the message displayed by XtremWeb, extract the XtremWeb job id
    //------------------------------------------------------------------------
    pos_xwid  = (returned_values.message).find("xw://");
    if ( pos_xwid == string::npos )
    {
      LOG(LOG_NOTICE, "%s(%s)    Job '%s'  'xw://' NOT found inside message "
                      "displayed by XtremWeb",
                      function_name, name.c_str(), bridge_job_id);
      current_job->setStatus(Job::ERROR);
      return;
    }
    
    xw_job_id = (returned_values.message).substr(pos_xwid,
                          (returned_values.message).length() - pos_xwid - 1);
    // Trim xw_job_id
    size_t pos_end = xw_job_id.find_last_not_of(" \t\n");
    if ( pos_end != string::npos )
        xw_job_id = xw_job_id.substr(0, pos_end+1);
    
    //------------------------------------------------------------------------
    // Insert the XtremWeb job id in the database
    // Set the bridge status of the job to RUNNING
    //------------------------------------------------------------------------
    current_job->setGridId(xw_job_id);
    current_job->setStatus(Job::RUNNING);
    
    LOG(LOG_INFO, "%s(%s)    Job '%s'  XtremWeb Job ID = '%s'",
                  function_name, name.c_str(), bridge_job_id, xw_job_id.c_str());
  }
  LOG(LOG_INFO, "%s(%s)  %d job(s) have been submitted to XtremWeb",
                function_name, name.c_str(), jobs.size());
}


//============================================================================
//  Member function  updateStatus
//============================================================================
void XWHandler::updateStatus(void) throw (BackendException *)
{
  const char * function_name = "XWHandler::updateStatus";
  
  LOG(LOG_DEBUG, "%s(%s)  Updating status of jobs  -  Begin",
                 function_name, name.c_str());
  
  //OBSOLETE:  createCFG();
  
  DBHandler * jobDB = DBHandler::get();
  jobDB->pollJobs(this, Job::RUNNING, Job::CANCEL);
  DBHandler::put(jobDB);
  
  LOG(LOG_DEBUG, "%s(%s)  Updating status of jobs  -  End",
                 function_name, name.c_str());
}


//============================================================================
//  Member function  updateJob
//============================================================================
void XWHandler::updateJob(Job * job, string xw_job_current_status)
{
  const char * function_name = "XWHandler::updateJob";
  
  const struct
  {
      string         xw_job_status;
      Job::JobStatus bridge_job_status;
  } xw_to_bridge_status_relation[] =
  {
    {"WAITING",     Job::RUNNING},
    {"PENDING",     Job::RUNNING},
    {"RUNNING",     Job::RUNNING},
    {"DATAREQUEST", Job::RUNNING},
    {"AVAILABLE",   Job::RUNNING},
    {"RUNNING",     Job::RUNNING},
    {"COMPLETED",   Job::FINISHED},
    {"ERROR",       Job::ERROR},
    {"ABORTED",     Job::ERROR},
    {"LOST",        Job::ERROR},
    {"UNAVAILABLE", Job::ERROR},
    {"",            Job::INIT},
  };
  
  LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  '%s'  Updating bridge job status",
                 function_name, name.c_str(), job->getId().c_str(),
                 job->getGridId().c_str(), xw_job_current_status.c_str());
  
  for ( unsigned xw_status_no = 0;
        xw_to_bridge_status_relation[xw_status_no].xw_job_status != "";
        xw_status_no++ )
  {
    if ( xw_to_bridge_status_relation[xw_status_no].xw_job_status ==
         xw_job_current_status )
    {
      job->setStatus(xw_to_bridge_status_relation[xw_status_no].
                     bridge_job_status);
      break;
    }
  }
  
  LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  '%s'  Updated bridge job status",
                function_name, name.c_str(), job->getId().c_str(),
                job->getGridId().c_str(), xw_job_current_status.c_str());
}


//============================================================================
//  Member function  poll
//============================================================================
void XWHandler::poll(Job * job) throw (BackendException *)
{
  const char * function_name = "XWHandler::poll";

  xw_returned_t returned_values;
  
  string bridge_job_id = job->getId();
  string xw_job_id     = job->getGridId();
  
  LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)", function_name, name.c_str(),
                 bridge_job_id.c_str(), xw_job_id.c_str());
  
  xw_command_t  xw_command;
  unsigned char bridge_status = job->getStatus();
  
  //--------------------------------------------------------------------------
  // If the bridge status of the job is CANCEL, delete it from XtremWeb
  //--------------------------------------------------------------------------
  if ( bridge_status == Job::CANCEL )
  {
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  'CANCEL'",
                  function_name, name.c_str(), bridge_job_id.c_str(),
                  xw_job_id.c_str());
    
    xw_command.xwcommand   = XW_RM;
    xw_command.xwjobid     = xw_job_id;
    xw_command.bridgejobid = bridge_job_id;
    returned_values        = xtremwebClient(xw_command);
    
    //------------------------------------------------------------------------
    // If and only if cancellation is successful, update the bridge job status
    //------------------------------------------------------------------------
    if ( returned_values.retcode != 0 )
    {
      LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb return code = x'%X' --> "
                    "%d  -->  3G Bridge status left unchanged",
                    function_name, name.c_str(), bridge_job_id.c_str(),
                    xw_job_id.c_str(), returned_values.retcode,
                    returned_values.retcode / 256);
    }
    else
    {
      LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  successfully removed from "
                    "XtremWeb",
                    function_name, name.c_str(), bridge_job_id.c_str(),
                    xw_job_id.c_str());
      job->setStatus(Job::ERROR);
    }
  }
  
  //--------------------------------------------------------------------------
  // If the bridge status of the job is RUNNING, refresh it from XtremWeb
  //--------------------------------------------------------------------------
  if ( bridge_status == Job::RUNNING )
  {
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  'RUNNING'",
                  function_name, name.c_str(), bridge_job_id.c_str(),
                  xw_job_id.c_str());
    
#ifdef XW_CLEAN_FORCED
    //------------------------------------------------------------------------
    // Force XtremWeb to refresh its cache.
    // This should NOT be necessary anymore.
    //------------------------------------------------------------------------
    xw_command.xwcommand          = XW_CLEAN;
    string * xw_displayed_message = xtremwebClient(xw_command);
#endif
    
    //------------------------------------------------------------------------
    // Query job status from XtremWeb
    //------------------------------------------------------------------------
    xw_command.xwcommand   = XW_STATUS;
    xw_command.xwjobid     = xw_job_id;
    xw_command.bridgejobid = bridge_job_id;
    returned_values        = xtremwebClient(xw_command);
    
    //------------------------------------------------------------------------
    // Try to parse the message displayed by XtremWeb
    //------------------------------------------------------------------------
    if ( (returned_values.message).find("ERROR : object not found") !=
         string::npos )
    {
      LOG(LOG_NOTICE, "%s(%s)    Job '%s' (%s)  NOT found by XtremWeb",
                      function_name, name.c_str(), bridge_job_id.c_str(),
                      xw_job_id.c_str());
      job->setStatus(Job::ERROR);
      return;
    }
    
    if ( returned_values.retcode != 0 )
    {
      LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb return code = x'%X' --> "
                    "%d  -->  3G Bridge status left unchanged",
                    function_name, name.c_str(), bridge_job_id.c_str(),
                    xw_job_id.c_str(), returned_values.retcode,
                    returned_values.retcode / 256);
    }
    else
    {
      // Initial value of the 'xw_job_status' variable is the empty string
      string xw_job_status = "";
      
      // Message displayed by XtremWeb should contain:  STATUS='<status>'
      size_t pos_status = (returned_values.message).find("STATUS='");
      if ( pos_status != string::npos )
      {
        size_t pos_quote = (returned_values.message).find("'",pos_status + 8);
        if ( pos_quote != string::npos )
          xw_job_status = (returned_values.message).substr(pos_status + 8,
                          pos_quote - (pos_status+8) );
      }
      
      //----------------------------------------------------------------------
      // If relevant, use XtremWeb job status to update bridge job status
      //----------------------------------------------------------------------
      if ( xw_job_status == "" )
      {
        LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb status NOT found -->"
                      "3G Bridge status left unchanged",
                      function_name, name.c_str(), bridge_job_id.c_str(),
                      xw_job_id.c_str());
      }
      else
      {
        LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb status = '%s'",
                      function_name, name.c_str(), bridge_job_id.c_str(),
                      xw_job_id.c_str(), xw_job_status.c_str());
      
        updateJob(job, xw_job_status);
      
        // If the XtremWeb status of the job is COMPLETED,
        //   set its bridge status to FINISHED
        if ( xw_job_status == "COMPLETED" )
          bridge_status = Job::FINISHED;
      }
    }
  }
  
  //------------------------------------------------------------------------
  // If the bridge status of the job is FINISHED, retrieve its results
  // from XtremWeb, and tell the 3G Bridge of the file(s) location
  //------------------------------------------------------------------------
  if ( bridge_status == Job::FINISHED )
  {
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  'FINISHED'",
                  function_name, name.c_str(), bridge_job_id.c_str(),
                  xw_job_id.c_str());
    
    xw_command.xwcommand   = XW_RESULT;
    xw_command.xwjobid     = xw_job_id;
    xw_command.bridgejobid = bridge_job_id;
    returned_values        = xtremwebClient(xw_command);
    
    if ( returned_values.retcode != 0 )
    {
      LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb return code = x'%X' --> "
                    "%d  -->  3G Bridge status left unchanged",
                    function_name, name.c_str(), bridge_job_id.c_str(),
                    xw_job_id.c_str(), returned_values.retcode,
                    returned_values.retcode / 256);
    }
    else
    {
      LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  Results successfully retrieved "
                    "from XtremWeb  '%s'",
                    function_name, name.c_str(), bridge_job_id.c_str(),
                    xw_job_id.c_str(), (returned_values.message).c_str());
    
#ifdef XW_SLEEP_BEFORE_DOWNLOAD
      // Sleep some time before download.  Should NOT be necessary.
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  Sleeping time before download = %d",
                     function_name, name.c_str(), bridge_job_id.c_str(),
                     g_sleep_time_before_download);
      sleep(g_sleep_time_before_download);//waiting for results file downloaded
#endif
      
      // Name of ZIP file contains characters of XtremWeb job id after last '/'
      size_t pos_id = xw_job_id.rfind('/') + 1;
      LOG(LOG_DEBUG, "%s(%s)  xw_job_id = '%s'  pos_id=%d  xw_id_len=%d",
                     function_name, name.c_str(), xw_job_id.c_str(), pos_id,
                     xw_job_id.length() - pos_id);
      string xw_zip_file_name = "*_ResultsOf_" +
                 xw_job_id.substr(pos_id, xw_job_id.length() - pos_id) + ".zip";
      
      //------------------------------------------------------------------------
      // The XtremWeb ZIP file containing the job results should be in the
      // XtremWeb file folder.  Rename this ZIP file using the bridge job id.
      //------------------------------------------------------------------------
      string zip_command = "mv " + g_xw_files_folder + "/" + xw_zip_file_name +
                           " "   + g_xw_files_folder + "/" + bridge_job_id +
                           ".zip";
      LOG(LOG_DEBUG, "%s(%s)  command = '%s'",
                     function_name, name.c_str(), zip_command.c_str());
      
      returned_values.retcode = system(zip_command.c_str());
      if ( returned_values.retcode != 0 )
      {
        LOG(LOG_ERR, "%s(%s)    Job '%s'  return_code = %d  for command '%s'",
                     function_name, name.c_str(), bridge_job_id.c_str(),
                     returned_values.retcode, zip_command.c_str());
        returned_values.message = "Error on command '" + zip_command + "'";
      }
      
      //------------------------------------------------------------------------
      // Get the list of bridge ouptut files to be extracted from the XtremWeb
      // ZIP file to the bridge output files folder
      //------------------------------------------------------------------------
      zip_command = "/usr/bin/unzip -o " + g_xw_files_folder + "/" +
                    bridge_job_id + ".zip ";
      
      auto_ptr< vector<string> > output_files = job->getOutputs();
      for (vector<string>::iterator file_iterator = output_files->begin();
           file_iterator != output_files->end(); file_iterator++)
        zip_command += *file_iterator + " " ;
      
      zip_command += "-d " + g_bridge_output_folder + "/" +
                     bridge_job_id.substr(0, 2) + "/" + bridge_job_id;
      LOG(LOG_DEBUG, "%s(%s)  command = '%s'",
                     function_name, name.c_str(), zip_command.c_str());
      
      //------------------------------------------------------------------------
      // Extract the bridge output files from the ZIP file containing results
      //------------------------------------------------------------------------
      returned_values.retcode = system(zip_command.c_str());
      
      if ( returned_values.retcode != 0 )
      {
        LOG(LOG_NOTICE,"%s(%s)    Job '%s'  return_code = %d  for command '%s'",
                       function_name, name.c_str(), bridge_job_id.c_str(),
                       returned_values.retcode, zip_command.c_str());
        returned_values.message = "Error on command '" + zip_command + "'";
      }
      
    }
  }
}


//============================================================================
//  Factory function
//============================================================================
HANDLER_FACTORY(config, instance)
{
  return new XWHandler(config, instance);
}
