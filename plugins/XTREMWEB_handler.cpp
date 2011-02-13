/*
 * Copyright (C) 2011      LAL, Univ Paris-Sud, IN2P3/CNRS  (Etienne URBAH)
 * Copyright (C) 2008-2010 MTA SZTAKI LPDS
 *
 * XtremWeb-HEP plugin for the 3G Bridge
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

#include <dirent.h>


using namespace std;

//XW_CLEAN_FORCED adds an preparation call to xwclean
//This is a workaround to some bug in XtremWeb
//#define XW_CLEAN_FORCED

//#define XW_SLEEP_BEFORE_DOWNLOAD

enum xw_command_enumeration
{
  XW_CLEAN,
  XW_DOWNLOAD,
  XW_RESULT,
  XW_RM,
  XW_STATUS,
  XW_SENDDATA,
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

struct returned_t
{
  int    retcode;
  string message;
};


//============================================================================
//  Global variables
//============================================================================
string g_xw_client_bin_folder;        // XtremWeb-HEP client binaries folder
string g_xw_files_folder;             // Folder for XW input and output files
int    g_sleep_time_before_download;  // Sleeping time before download in s


//============================================================================
//  Remark about string.c_str()
//    This member function does NOT allocate new storage, but only returns a
//    const char pointer to an already allocated storage.
//    Therefore, there is NO need to free this storage.
//    But this works only on a lvalue, and only during the lvalue lifetime.
//    It FAILS on expressions and after return from a function !
//============================================================================


//============================================================================
//
//  Constructor
//
//============================================================================
XWHandler::XWHandler(GKeyFile * config, const char * instance)
  throw (BackendException *): GridHandler(config, instance)
{
  const char * function_name = "XWHandler::XWHandler";
  const char * instance_name = name.c_str();
  
  LOG(LOG_INFO, "%s(%s)  Plugin creation for instance   '%s'",
                function_name, instance_name, instance);
  
  groupByNames = false;
  
  //--------------------------------------------------------------------------
  // xwclient_install_dir
  //--------------------------------------------------------------------------
  DIR *  directory_stream;
  string xw_client_folder_str;
  
  char * xw_client_folder = g_key_file_get_string(config, instance,
                                                "xwclient_install_dir", NULL);
  if ( xw_client_folder )
  {
    g_strstrip(xw_client_folder);
    xw_client_folder_str = xw_client_folder;
  }
  //--------------------------------------------------------------------------
  // If 'xwclient_install_dir' is not given, search '/opt/xwhep-client-i.j.k'
  //--------------------------------------------------------------------------
  else
  {
    const char * xw_root              = "/opt";
    const string xw_client_prefix_str = "xwhep-client-";
    
    size_t       xw_client_prefix_length = xw_client_prefix_str.length();
    dirent *     directory_entry;
    const char * file_name;
    string       file_name_str;
    size_t       pos1;
    size_t       pos2;
    string       number_str;
    int          major = 0;
    int          minor = 0;
    int          micro = 0;
    int          i, j, k;
    string       xw_file_name_str = "";
      
    directory_stream = opendir(xw_root);
    if ( ! directory_stream )
      throw new BackendException("XWHandler::XWHandler  can NOT read folder "
                                 "'%s'", xw_root);
    else
    {
      while ( (directory_entry=readdir(directory_stream)) != 0 )
      {
        //LOG(LOG_DEBUG, "%s(%s)   XW client install folder:      '%s' found "
        //               "in '%s'", function_name, instance_name,
        //               directory_entry->d_name, xw_root);
        
        file_name     = directory_entry->d_name;
        file_name_str = string(file_name);
        if ( file_name_str.substr(0, xw_client_prefix_length) ==
             xw_client_prefix_str )
        {
          //LOG(LOG_DEBUG, "%s(%s)   XW client install folder:      '%s' "
          //               "begins with '%s'", function_name, instance_name,
          //               file_name, xw_client_prefix_str.c_str());
          
          pos1 = file_name_str.find_first_of("0123456789");
          if ( pos1 == string::npos )
            continue;
          pos2 = file_name_str.find_first_not_of("0123456789", pos1);
          if ( pos2 == string::npos )
            continue;
          number_str = file_name_str.substr(pos1, pos2 - pos1);
          i = atoi(number_str.c_str());
          if ( i < major )
            continue;
          LOG(LOG_DEBUG, "%s(%s)   XW client install folder:      '%s' :  "
                         "Major = %d",
                         function_name, instance_name, file_name, i);
          
          pos1 = file_name_str.find_first_of("0123456789", pos2);
          if ( pos1 == string::npos )
            continue;
          pos2 = file_name_str.find_first_not_of("0123456789", pos1);
          if ( pos2 == string::npos )
            continue;
          number_str = file_name_str.substr(pos1, pos2 - pos1);
          j = atoi(number_str.c_str());
          if ( j < minor )
            continue;
          LOG(LOG_DEBUG, "%s(%s)   XW client install folder:      '%s' :  "
                         "Minor = %d",
                         function_name, instance_name, file_name, j);
          
          pos1 = file_name_str.find_first_of("0123456789", pos2);
          if ( pos1 == string::npos )
            continue;
          number_str = file_name_str.substr(pos1);
          k = atoi(number_str.c_str());
          if ( k < micro )
            continue;
          LOG(LOG_DEBUG, "%s(%s)   XW client install folder:      '%s' :  "
                         "Micro = %d",
                         function_name, instance_name, file_name, k);
          
          major = i;
          minor = j;
          micro = k;
          xw_file_name_str = file_name_str;
          LOG(LOG_DEBUG, "%s(%s)   XW client install folder:      '%s' is "
                         "candidate with %d.%d.%d",
                         function_name, instance_name, file_name, i, j, k);
        }
      }
      closedir(directory_stream);
      
      if ( xw_file_name_str == "" )
        throw new BackendException("XWHandler::XWHandler  can NOT find '%s'",
                                   xw_client_prefix_str.c_str());
      
      xw_client_folder_str = string(xw_root) + "/" + xw_file_name_str;
    }
  }
  
  //--------------------------------------------------------------------------
  // 'xwclient_install_dir' was given or has been found.
  // Verify that this is a readable folder.
  //--------------------------------------------------------------------------
  LOG(LOG_INFO, "%s(%s)  XW client install folder:      '%s'",
                function_name, instance_name, xw_client_folder_str.c_str());
  g_xw_client_bin_folder = xw_client_folder_str + "/bin/";
  const char * xw_client_bin_folder = g_xw_client_bin_folder.c_str();
  LOG(LOG_INFO, "%s(%s)  XW client binaries folder:     '%s'",
                function_name, instance_name, xw_client_bin_folder);
  
  directory_stream = opendir(xw_client_bin_folder);
  if ( directory_stream )
    closedir(directory_stream);
  else
    throw new BackendException("XWHandler::XWHandler  can NOT read folder "
                               "'%s'", xw_client_bin_folder);
  
  //--------------------------------------------------------------------------
  // Test file name = this.name + pid + TEST
  //--------------------------------------------------------------------------
  char         pid[33];
  string       test_file_path_str;
  const char * test_file_path;
  FILE *       test_file;
  sprintf(pid, "%d", getpid());
  const string test_file_name_str = "/" + name + "_" + pid + "_test.tmp";
  
  //--------------------------------------------------------------------------
  // xw_files_dir
  //--------------------------------------------------------------------------
  char * xw_files_folder = g_key_file_get_string(config, instance,
                                                 "xw_files_dir", NULL);
  if ( ! xw_files_folder )
    throw new BackendException("XWHandler::XWHandler  No XtremWeb files "
                               "folder specified for '%s'", instance);
  
  g_strstrip(xw_files_folder);
  LOG(LOG_INFO, "%s(%s)  Folder for XtremWeb files:     '%s'",
                function_name, instance_name, xw_files_folder);
  
  g_xw_files_folder  = string(xw_files_folder);
  test_file_path_str = g_xw_files_folder + test_file_name_str;
  test_file_path     = test_file_path_str.c_str();
  LOG(LOG_DEBUG, "%s(%s)   Path for write access test:  '%s'",
                 function_name, instance_name, test_file_path);
  
  test_file = fopen(test_file_path, "w");
  if ( test_file )
    fclose(test_file);
  else
    throw new BackendException("XWHandler::XWHandler  can NOT write to "
                               "folder '%s'", xw_files_folder);
  unlink(test_file_path);
  
  //--------------------------------------------------------------------------
  // [wssubmitter]output-dir
  //--------------------------------------------------------------------------
  const char * bridge_output_folder = g_key_file_get_string(config,
                                           "wssubmitter", "output-dir", NULL);
  if ( ! bridge_output_folder )
    throw new BackendException("XWHandler::XWHandler  No bridge output "
                               "files folder specified for '%s'", instance);
  
  LOG(LOG_INFO, "%s(%s)  3G Bridge output folder:       '%s'",
                function_name, instance_name, bridge_output_folder);
  
  test_file_path_str = string(bridge_output_folder) + test_file_name_str;
  test_file_path     = test_file_path_str.c_str();
  LOG(LOG_DEBUG, "%s(%s)   Path for write access test:  '%s'",
                 function_name, instance_name, test_file_path);
  
  test_file = fopen(test_file_path, "w");
  if ( test_file )
    fclose(test_file);
  else
    throw new BackendException("XWHandler::XWHandler  can NOT write to "
                               "folder '%s'", bridge_output_folder);
  unlink(test_file_path);
  
#ifdef XW_SLEEP_BEFORE_DOWNLOAD
  //--------------------------------------------------------------------------
  // xw_sleep_time_before_download
  //--------------------------------------------------------------------------
  const char * sleep_time_before_download = g_key_file_get_string(config,
                             instance, "xw_sleep_time_before_download", NULL);
  if ( ! sleep_time_before_download )
    throw new BackendException("XWHandler::XWHandler  No sleeping time "
                               "before download specified for '%s'",
                               instance);
  
  LOG(LOG_INFO, "%s(%s)  Sleeping time before download: '%s'", function_name,
                instance_name, sleep_time_before_download);
  g_sleep_time_before_download = atoi(sleep_time_before_download);
#endif
}


//============================================================================
//
//  Destructor
//
//============================================================================
XWHandler::~XWHandler()
{
  const char * function_name = "XWHandler::~XWHandler";
  const char * instance_name = name.c_str();
  
  LOG(LOG_DEBUG, "%s(%s)  Successfully called for destruction",
                 function_name, instance_name);
}


//============================================================================
//
//  Function  TrimEol     
//  @param  my_string  Reference to a string
//  @return            void
//
//  This function removes ' \t\r\n' characters from the end of the string
//  given in parameter
//
//============================================================================
void TrimEol(string & my_string)
{
  size_t pos_end = my_string.find_last_not_of(" \t\r\n");
  if      ( pos_end == string::npos )
    my_string.clear();
  else if ( pos_end + 1 < my_string.length() )
    my_string.erase(pos_end + 1);
}


//============================================================================
//
//  Function  OutputAndErrorFromCommand     
//  @param  command_str  String containing the command to execute
//  @param  params_str   String containing the parameters to the command
//  @return              Struct containing return code and message displayed
//
//  This function executes the command given in parameter, retrieves the
//  message normally displayed by the command on STDOUT + STDERR, and returns
//  a structure containing the return code and this message.
//
//============================================================================
returned_t OutputAndErrorFromCommand(const string command_str,
                                     const string params_str)
{
  const char * function_name = "XWHandler  OutputAndErrorFromCommand";
  const char * instance_name = "";
  
  const char * command       = command_str.c_str();
  const char * params        = params_str.c_str();
  LOG(LOG_DEBUG, "%s(%s)  Command = '%s'  Params = '%s'",
                 function_name, instance_name, command, params);
  
  int        pipe_std_out_err[2];
  pid_t      process_id;
  int        return_code;
  returned_t returned_values;
  
  //--------------------------------------------------------------------------
  //  Create a pipe permitting to retrieve STDOUT and STDERR of child process
  //  Create child process to execute the command
  //--------------------------------------------------------------------------
  pipe(pipe_std_out_err);
  
  process_id = fork();
  
  if ( process_id < 0)
  {
    char error_buffer[1024];
    sprintf(error_buffer, "fork() return_code = %d  errno = %d",
                          process_id, errno);
    LOG(LOG_ERR, "%s(%s)  %s", function_name, instance_name, error_buffer);
    returned_values.retcode = process_id;
    returned_values.message = error_buffer;
    return returned_values;
  }
  
  if ( process_id == 0)
  {
    //------------------------------------------------------------------------
    //  Current process is the child.  Close pipe input.
    //  Redirect child STDOUT and STDERR to the pipe output.
    //------------------------------------------------------------------------
    close(pipe_std_out_err[0]);
    dup2(pipe_std_out_err[1], STDOUT_FILENO);
    dup2(pipe_std_out_err[1], STDERR_FILENO);
    
    //------------------------------------------------------------------------
    char   shell_path[] = "/bin/sh";
    char   shell[]      = "sh";
    
    if ( command_str.substr(0, g_xw_client_bin_folder.length()) ==
         g_xw_client_bin_folder )
    {
      //----------------------------------------------------------------------
      //  Shell scripts are submitted to 'sh' with 2 parameters
      //----------------------------------------------------------------------
      size_t command_length = command_str.length() + 1;
      size_t params_length  = params_str.length()  + 1;
      char * command_buf    = new char[command_length];
      char * params_buf     = new char[params_length];
      
      char * args[] = { shell,
                        strcpy(command_buf, command),
                        strcpy(params_buf,  params),
                        (char *)NULL };
      LOG(LOG_DEBUG, "%s(%s)  Child process:  execv(%s)  '%s' '%s' '%s'",
                     function_name, instance_name, shell_path,
                     args[0], args[1], args[2]);
      return_code = execv(shell_path, args);
      
      LOG(LOG_DEBUG, "%s(%s)  Child process:  execv(%s)  '%s' '%s' '%s'",
                     function_name, instance_name, shell_path,
                     args[0], args[1], args[2]);
      return_code = execv(shell_path, args);
    }
    else
    {
      //----------------------------------------------------------------------
      //  Binaries are submitted to 'sh -c' with 1 parameter
      //  (This should also work for shell scripts)
      //----------------------------------------------------------------------
      char         option[]          = "-c";
      string       cmd_params_str    = command_str + " " + params_str;
      const char * cmd_params        = cmd_params_str.c_str();
      size_t       cmd_params_length = cmd_params_str.length() + 1;
      char *       cmd_params_buf    = new char[cmd_params_length];
      
      char *       args[] = { shell, option,
                              strcpy(cmd_params_buf, cmd_params),
                              (char *)NULL };
      
      LOG(LOG_DEBUG, "%s(%s)  Child process:  execv(%s)  '%s' '%s' '%s'",
                     function_name, instance_name, shell_path,
                     args[0], args[1], args[2]);
      return_code = execv(shell_path, args);
    }
    
    LOG(LOG_ERR, "%s(%s)  Child process:  execv return_code = %d  errno = %d",
                 function_name, instance_name, return_code, errno);
    exit(return_code);
  }
  
  //--------------------------------------------------------------------------
  //  Wait until child has finished
  //--------------------------------------------------------------------------
  (void)wait(&(returned_values.retcode));
  close(pipe_std_out_err[1]);
  
  LOG(LOG_DEBUG, "%s(%s)  '%s' return code = x'%X' --> %d",
                 function_name, instance_name, command,
                 returned_values.retcode, returned_values.retcode / 256);
  
  //--------------------------------------------------------------------------
  //  Read pipe to retrieve STDOUT and STDERR of forked child
  //--------------------------------------------------------------------------
  int    count;
  size_t buffer_size = 1024;
  char   message_buffer[buffer_size];
  
  memset(message_buffer, 0, buffer_size);
  
  while ( (count=read(pipe_std_out_err[0], message_buffer, buffer_size)) > 0 )
  {
    (returned_values.message).append(message_buffer);
    memset(message_buffer, 0, buffer_size);
  }
  close(pipe_std_out_err[0]);
  
  //--------------------------------------------------------------------------
  //  Return the structure containing the return code and the message
  //--------------------------------------------------------------------------
  TrimEol(returned_values.message);
  LOG(LOG_DEBUG, "%s(%s)  Displayed message = '%s'", function_name,
                 instance_name, (returned_values.message).c_str());
  
  return returned_values;
}


//============================================================================
//
//  Function  xtremwebClient     
//  @param  xw_command  Struct containing the XtremWeb command type and params
//  @return             Struct containing return code and message displayed
//
//  This function executes the XtremWeb command given in parameter, retrieves
//  the message normally displayed by the command on STDOUT + STDERR, and
//  returns a structure containing the return code and this message.
//
//============================================================================
returned_t xtremwebClient(const xw_command_t xw_command)
{
  const char * function_name = "XWHandler  xtremwebClient";
  const char * instance_name = "";
  
  char *       cwd;
  const char * workdir;
  
  LOG(LOG_DEBUG, "%s(%s)  XtremWeb command num = %d",
                 function_name, instance_name, xw_command.xwcommand);
  
  //--------------------------------------------------------------------------
  //  Depending on the XtremWeb command, build the adequate command and params
  //  Execute the XtremWeb command, and return the displayed message
  //--------------------------------------------------------------------------
  switch ( xw_command.xwcommand )
  {
    case XW_CLEAN:
      return OutputAndErrorFromCommand(g_xw_client_bin_folder + "xwclean",
                                       "");
    
    case XW_RM:
      return OutputAndErrorFromCommand(g_xw_client_bin_folder + "xwrm",
                                       xw_command.xwjobid);
    
    case XW_STATUS:
      return OutputAndErrorFromCommand(g_xw_client_bin_folder + "xwstatus",
                                       xw_command.xwjobid);
    
    case XW_SENDDATA:
      return OutputAndErrorFromCommand(g_xw_client_bin_folder + "xwsenddata",
                                       xw_command.name + " " +
                                       xw_command.path);
    
    case XW_SUBMIT:
      return OutputAndErrorFromCommand(g_xw_client_bin_folder + "xwsubmit",
                                       xw_command.appname + " " +
                                       xw_command.appargs + " " +
                                       xw_command.appenv);
    
    case XW_DOWNLOAD:
    {
      workdir     = g_xw_files_folder.c_str();
      LOG(LOG_DEBUG, "%s(%s)  Executing chdir(%s)",
                     function_name, instance_name, workdir);
      chdir(workdir);
      
      return OutputAndErrorFromCommand(g_xw_client_bin_folder + "xwdownload",
                                       xw_command.appname + " " +
                                       xw_command.appargs + " " +
                                       xw_command.xwjobid + " " +
                                       xw_command.appenv);
    }
    
    case XW_RESULT:
    {
      cwd = getcwd((char*)NULL, 0);
      LOG(LOG_DEBUG, "%s(%s)  cwd = '%s'",
                     function_name, instance_name, cwd);
      
      workdir = g_xw_files_folder.c_str();
      if  ( strcmp(cwd, workdir) != 0 )
      {
        LOG(LOG_DEBUG, "%s(%s)  Executing chdir('%s')",
                       function_name, instance_name, workdir);
        chdir(workdir);
      }
      
      free(cwd);             // 'cwd' has been dynamically allocated by getcwd
      
      return OutputAndErrorFromCommand(g_xw_client_bin_folder + "xwresults",
                                       xw_command.xwjobid);
    }
  }
  
  returned_t returned_error = {-1, "XtremWeb command num %d not provisioned"};
  return returned_error;
}


//============================================================================
//
//  Function  setJobStatusToError     
//  @param  function_name  Name of the calling function
//  @param  instance_name  Name of the instance for the calling function
//  @param  bridge_job_id  ID of the current bridge job
//  @param  job            current bridge job
//  @return                NOTHING
//
//  This function logs a NOTICE message and sets the status of the bridge job
//  to ERROR.
//
//============================================================================
void setJobStatusToError(const char * function_name,
                         const char * instance_name,
                         const char * bridge_job_id,
                         Job *        job)
{
  LOG(LOG_NOTICE, "%s(%s)  Job '%s'  Set status of bridge job to 'ERROR'",
                  function_name, instance_name, bridge_job_id);
  job->setStatus(Job::ERROR);
}


//============================================================================
//
//  Member function  submitJobs
//
//============================================================================
void XWHandler::submitJobs(JobVector &jobs) throw (BackendException *)
{
  const char * function_name = "XWHandler::submitJobs";
  const char * instance_name = name.c_str();
  
  LOG(LOG_INFO, "%s(%s)  Number of job(s) to be submitted to XtremWeb :  %d",
                function_name, instance_name, jobs.size());
  
  if ( jobs.size() < 1 )
      return;
  
  size_t        nb_jobs = 0;
  Job *         job;
  const char *  bridge_job_id;
  string        input_file_paths_str;
  string        input_file_name_str;
  string        input_file_path_str;
  const char *  input_file_name;
  const char *  input_file_path;
  auto_ptr< vector<string> > input_files;
  bool          b_xw_data_error;
  FileRef       input_file_ref;
  string        input_file_md5_str;
  const char *  input_file_md5;
  int           input_file_size;
  string        xw_data_file_path_str;
  const char *  xw_data_file_path;
  FILE *        xw_data_file;
  char          xw_data_xml[1024];
  string        xw_data_id_str;
  const char *  xw_data_id;
  string        zip_file_name_str;
  const char *  zip_file_name;
  string        zip_command_str;
  string        zip_params_str;
  string        job_batch_id_str;
  string        xw_group_id_str;
  xw_command_t  xw_submit;
  returned_t    returned_values;
  const char *  returned_message;
  size_t        pos_xwid;
  string        xw_job_id_str;
  const char *  xw_job_id;
  
  //------------------------------------------------------------------------
  // Loop on jobs to be submitted
  //------------------------------------------------------------------------
  for ( JobVector::iterator job_iterator = jobs.begin();
        job_iterator != jobs.end();
        job_iterator++ )
  {
    job               = *job_iterator;
    bridge_job_id     = (job->getId()).c_str();
    xw_submit.appenv  = "";
    
    //------------------------------------------------------------------------
    // Retrieve list of input files
    //------------------------------------------------------------------------
    input_file_paths_str = "";
    input_files          = job->getInputs();
    b_xw_data_error      = false;
    for ( vector<string>::iterator input_file_iterator = input_files->begin();
          input_file_iterator != input_files->end();
          input_file_iterator++ )
    {
      input_file_name_str = *input_file_iterator;
      input_file_path_str = job->getInputPath(input_file_name_str);
      input_file_name     = input_file_name_str.c_str();
      input_file_path     = input_file_path_str.c_str();
      
      if ( input_file_path_str[0] == '/' )
      //----------------------------------------------------------------------
      // Local files have to be stored together inside a ZIP
      //----------------------------------------------------------------------
      {
        LOG(LOG_DEBUG, "%s(%s)  Job '%s'  input_file_name = '%s'  "
                       "input_file_path = '%s'", function_name, instance_name,
                       bridge_job_id, input_file_name, input_file_path);
        input_file_paths_str += " " + input_file_path_str;
      }
      else
      //----------------------------------------------------------------------
      // URLS have to be specified to XtremWeb as data
      //----------------------------------------------------------------------
      {
        // Following line works only if URL = schema://server/basename
        // xw_submit.appenv  = "  --xwenv " + input_file_path_str;
        
        //--------------------------------------------------------------------
        // Get MD5 and size for the URL
        //--------------------------------------------------------------------
        input_file_ref        = job->getInputRef(input_file_name_str);
        input_file_md5_str    = input_file_ref.getMD5();
        input_file_size       = input_file_ref.getSize();
        input_file_md5        = input_file_md5_str.c_str();
        xw_data_file_path_str = g_xw_files_folder +  "/" + bridge_job_id +
                                "_" + input_file_name_str + ".xml";
        xw_data_file_path     = xw_data_file_path_str.c_str();
        LOG(LOG_DEBUG, "%s(%s)  Job '%s'  input_file_name = '%s'  "
                       "input_file_url = '%s'  MD5 = '%s'  size = %d  "
                       "xw_data_file_path = '%s'",
                       function_name, instance_name, bridge_job_id,
                       input_file_name, input_file_path, input_file_md5,
                       input_file_size, xw_data_file_path);
        
        //--------------------------------------------------------------------
        // Create the XML file describing XtremWeb data
        //--------------------------------------------------------------------
        xw_data_file = fopen(xw_data_file_path, "w");
        if ( ! xw_data_file )
        {
          LOG(LOG_ERR, "%s(%s)  Job '%s'  input_file_name = '%s'  "
                       "input_file_path = '%s'  xw_data_file_path = '%s'  "
                       "I/O error = %d", function_name, instance_name,
                       bridge_job_id, input_file_name, input_file_path,
                       xw_data_file_path, errno);
          b_xw_data_error = true;
          break;
        }
        
        sprintf(xw_data_xml,
                "<data name=\"%s\" md5=\"%s\" size=\"%d\" uri=\"%s\"/>",
                input_file_name, input_file_md5, input_file_size,
                input_file_path);
        LOG(LOG_DEBUG, "%s(%s)  Job '%s'  xw_data_xml = '%s'",
                       function_name, instance_name, bridge_job_id,
                       xw_data_xml);
        fprintf(xw_data_file, "%s\n", xw_data_xml);
        fclose(xw_data_file);
    
        //--------------------------------------------------------------------
        // Send the data to XtremWeb
        //--------------------------------------------------------------------
        xw_submit.xwcommand = XW_SENDDATA;
        xw_submit.name      = "--xwxml";
        xw_submit.path      = xw_data_file_path_str ;
        returned_values     = xtremwebClient(xw_submit);
        returned_message    = (returned_values.message).c_str();
        
        LOG(LOG_DEBUG,"%s(%s)  Job '%s'  Data '%s' sent.  XtremWeb displayed "
                      "message = '%s'", function_name, instance_name,
                      bridge_job_id, input_file_name, returned_message);
        
        if ( returned_values.retcode != 0 )
        {
          LOG(LOG_NOTICE, "%s(%s)  Job '%s'  Data '%s'  return_code = x'%X' "
                          "--> %d  for XtremWeb data creation",
                          function_name, instance_name, bridge_job_id,
                          input_file_name, returned_values.retcode,
                          returned_values.retcode / 256);
          b_xw_data_error = true;
          break;
        }
        
        unlink(xw_data_file_path);
        
        //--------------------------------------------------------------------
        // From the message displayed by XtremWeb, extract the DATA id
        //--------------------------------------------------------------------
        pos_xwid = (returned_values.message).find("xw://");
        if ( pos_xwid == string::npos )
        {
          LOG(LOG_NOTICE, "%s(%s)  Job '%s'  'xw://' NOT found inside "
                          "message displayed by XtremWeb",
                          function_name, instance_name, bridge_job_id);
          b_xw_data_error = true;
          break;
        }
        
        xw_data_id_str = (returned_values.message).substr(pos_xwid,
                               (returned_values.message).length() - pos_xwid);
        TrimEol(xw_data_id_str);
        xw_data_id = xw_data_id_str.c_str();
        xw_submit.appenv  = "  --xwenv " + xw_data_id_str;
      }
    }
    
    if ( b_xw_data_error )
    {
      setJobStatusToError(function_name, instance_name, bridge_job_id, job);
      break;
    }
    
    //------------------------------------------------------------------------
    // Store all local input files inside a ZIP
    //------------------------------------------------------------------------
    if ( input_file_paths_str.length() > 0 )
    {
      zip_file_name_str = g_xw_files_folder + "/" + bridge_job_id + ".zip";
      zip_file_name     = zip_file_name_str.c_str();
      zip_command_str   = "/usr/bin/zip";
      zip_params_str    = "-v -j " + zip_file_name_str + " " +
                          input_file_paths_str;
      
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  %s %s",
                     function_name, instance_name, bridge_job_id,
                     zip_command_str.c_str(), zip_params_str.c_str());
      
      returned_values  = OutputAndErrorFromCommand(zip_command_str,
                                                   zip_params_str);
      returned_message = (returned_values.message).c_str();
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  return_code = x'%x' --> %d  for "
                     "zipping '%s'  ZIP displayed message = '%s'",
                     function_name, instance_name, bridge_job_id,
                     returned_values.retcode, returned_values.retcode / 256,
                     zip_file_name, returned_message);
      
      if ( returned_values.retcode != 0 )
      {
        LOG(LOG_ERR, "%s(%s)  Job '%s'  return_code = x'%x' --> %d  for "
                     "zipping '%s'  ZIP displayed message = '%s'",
                     function_name, instance_name, bridge_job_id,
                     returned_values.retcode, returned_values.retcode / 256,
                     zip_file_name, returned_message);
        setJobStatusToError(function_name, instance_name, bridge_job_id, job);
        break;
      }
      
      xw_submit.appenv += "  --xwenv " + zip_file_name_str;
    }
    
    //------------------------------------------------------------------------
    // If '_3G_BRIDGE_BATCH_ID' exists among the environment variables of the
    // bridge job, it should be an already existing XtremWeb 'group_id'
    //------------------------------------------------------------------------
    job_batch_id_str = job->getEnv("_3G_BRIDGE_BATCH_ID");
    if ( job_batch_id_str.length() > 0 )
    {
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  Job batch id = '%s'", function_name,
                     instance_name, bridge_job_id, job_batch_id_str.c_str());
      xw_group_id_str   = job_batch_id_str;
      xw_submit.appenv += "  --xwgroup " + xw_group_id_str;
    }
    
    //------------------------------------------------------------------------
    // Submit the job to XtremWeb
    //------------------------------------------------------------------------
    xw_submit.xwcommand = XW_SUBMIT;
    xw_submit.appname   = job->getName();
    xw_submit.appargs   = job->getArgs();
    returned_values     = xtremwebClient(xw_submit);
    returned_message    = (returned_values.message).c_str();
    
    LOG(LOG_DEBUG,"%s(%s)  Job '%s' submitted.  XtremWeb displayed message "
                  "= '%s'", function_name, instance_name, bridge_job_id,
                  returned_message);
    
    if ( returned_values.retcode != 0 )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                      "XtremWeb submission.  XtremWeb displayed message "
                      "= '%s'", function_name, instance_name, bridge_job_id,
                      returned_values.retcode, returned_values.retcode / 256,
                      returned_message);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job);
      break;
    }
    
    //------------------------------------------------------------------------
    // From the message displayed by XtremWeb, extract the XtremWeb job id
    //------------------------------------------------------------------------
    pos_xwid = (returned_values.message).find("xw://");
    if ( pos_xwid == string::npos )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s'  'xw://' NOT found inside message "
                      "displayed by XtremWeb",
                      function_name, instance_name, bridge_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job);
      break;
    }
    
    xw_job_id_str = (returned_values.message).substr(pos_xwid,
                               (returned_values.message).length() - pos_xwid);
    TrimEol(xw_job_id_str);
    xw_job_id = xw_job_id_str.c_str();
    
    //------------------------------------------------------------------------
    // Insert the XtremWeb job id in the database
    // Set the bridge status of the job to RUNNING
    //------------------------------------------------------------------------
    LOG(LOG_INFO, "%s(%s)  Job '%s'  Set status of bridge job to 'RUNNING'",
                  function_name, instance_name, bridge_job_id);
    job->setGridId(xw_job_id_str);
    job->setStatus(Job::RUNNING);
    
    LOG(LOG_INFO, "%s(%s)  Job '%s'  XtremWeb Job ID = '%s'",
                  function_name, instance_name, bridge_job_id, xw_job_id);
    logit_mon("event=job_submission job_id=%s job_id_dg=%s "
              "output_grid_name=%s", bridge_job_id, xw_job_id, instance_name);
    logit_mon("event=job_status job_id=%s status=Running", bridge_job_id);
    
    nb_jobs++;
  }
  LOG(LOG_INFO, "%s(%s)  Number of job(s) successfully submitted to "
                "XtremWeb :  %d / %d",
                function_name, instance_name, nb_jobs, jobs.size());
}


//============================================================================
//
//  Member function  updateStatus
//
//============================================================================
void XWHandler::updateStatus(void) throw (BackendException *)
{
  const char * function_name = "XWHandler::updateStatus";
  const char * instance_name = name.c_str();
  
  LOG(LOG_DEBUG, "%s(%s)  Updating status of jobs  -  Begin",
                 function_name, instance_name);
  
  //OBSOLETE:  createCFG();
  
  DBHandler * jobDB = DBHandler::get();
  jobDB->pollJobs(this, Job::RUNNING, Job::CANCEL);
  DBHandler::put(jobDB);
  
  LOG(LOG_DEBUG, "%s(%s)  Updating status of jobs  -  End",
                 function_name, instance_name);
}


//============================================================================
//
//  Member function  updateJob
//
//============================================================================
void XWHandler::updateJob(Job * job, string xw_job_current_status)
{
  const char * function_name = "XWHandler::updateJob";
  const char * instance_name = name.c_str();
  
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
  
  const char * bridge_job_id = (job->getId()).c_str();
  const char * xw_job_id     = (job->getGridId()).c_str();
  const char * xw_job_status = xw_job_current_status.c_str();
  
  LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  XtremWeb job status = '%s'  "
                 "Updating bridge job status", function_name, instance_name,
                 bridge_job_id, xw_job_id, xw_job_status);
  
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
  
  LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb job status = '%s'  "
                "Updated bridge job status", function_name, instance_name,
                bridge_job_id, xw_job_id, xw_job_status);
}


//============================================================================
//
//  Member function  poll
//
//============================================================================
void XWHandler::poll(Job * job) throw (BackendException *)
{
  const char * function_name = "XWHandler::poll";
  const char * instance_name = name.c_str();
  
  const string bridge_job_id_str = job->getId();
  const string xw_job_id_str     = job->getGridId();
  
  const char * bridge_job_id     = bridge_job_id_str.c_str();
  const char * xw_job_id         = xw_job_id_str.c_str();
  
  LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)",
                 function_name, instance_name, bridge_job_id, xw_job_id);

  returned_t    returned_values;
  const char *  returned_message;
  xw_command_t  xw_command;
  unsigned char bridge_status = job->getStatus();
  
  //--------------------------------------------------------------------------
  // If the bridge status of the job is CANCEL, delete it from XtremWeb
  //--------------------------------------------------------------------------
  if ( bridge_status == Job::CANCEL )
  {
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  Bridge job status = 'CANCEL'",
                  function_name, instance_name, bridge_job_id, xw_job_id);
    
    xw_command.xwcommand   = XW_RM;
    xw_command.xwjobid     = xw_job_id_str;
    xw_command.bridgejobid = bridge_job_id_str;
    returned_values        = xtremwebClient(xw_command);
    
    //------------------------------------------------------------------------
    // If and only if cancellation is successful, update the bridge job status
    //------------------------------------------------------------------------
    if ( returned_values.retcode != 0 )
    {
      LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb return code = x'%X' --> "
                    "%d  -->  3G Bridge status left unchanged",
                    function_name, instance_name, bridge_job_id, xw_job_id,
                    returned_values.retcode, returned_values.retcode / 256);
    }
    else
    {
      LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  successfully removed from "
                    "XtremWeb",
                    function_name, instance_name, bridge_job_id, xw_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job);
    }
  }
  
  //--------------------------------------------------------------------------
  // If the bridge status of the job is RUNNING, refresh it from XtremWeb
  //--------------------------------------------------------------------------
  if ( bridge_status == Job::RUNNING )
  {
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  Bridge job status = 'RUNNING'",
                  function_name, instance_name, bridge_job_id, xw_job_id);
    
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
    xw_command.xwjobid     = xw_job_id_str;
    xw_command.bridgejobid = bridge_job_id_str;
    returned_values        = xtremwebClient(xw_command);
    
    //------------------------------------------------------------------------
    // Try to parse the message displayed by XtremWeb
    //------------------------------------------------------------------------
    if ( (returned_values.message).find("ERROR : object not found") !=
         string::npos )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  NOT found by XtremWeb",
                      function_name, instance_name, bridge_job_id, xw_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job);
      return;
    }
    
    if ( returned_values.retcode != 0 )
    {
      LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb return code = x'%X' --> "
                    "%d  -->  3G Bridge status left unchanged",
                    function_name, instance_name, bridge_job_id, xw_job_id,
                    returned_values.retcode, returned_values.retcode / 256);
    }
    else
    {
      // Initial value of the 'xw_job_status' variable is the empty string
      string xw_job_status_str = "";
      
      // Message displayed by XtremWeb should contain:  STATUS='<status>'
      size_t pos_status = (returned_values.message).find("STATUS='");
      if ( pos_status != string::npos )
      {
        size_t pos_quote = (returned_values.message).find("'",pos_status + 8);
        if ( pos_quote != string::npos )
          xw_job_status_str = (returned_values.message).substr(pos_status + 8,
                              pos_quote - (pos_status+8) );
      }
      
      //----------------------------------------------------------------------
      // If relevant, use XtremWeb job status to update bridge job status
      //----------------------------------------------------------------------
      if ( xw_job_status_str == "" )
      {
        LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb status NOT found -->"
                      "3G Bridge status left unchanged",
                      function_name, instance_name, bridge_job_id, xw_job_id);
      }
      else
      {
        LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb status = '%s'",
                      function_name, instance_name, bridge_job_id, xw_job_id,
                      xw_job_status_str.c_str());
      
        updateJob(job, xw_job_status_str);
      
        // If the XtremWeb status of the job is COMPLETED,
        //   set its bridge status to FINISHED
        if ( xw_job_status_str == "COMPLETED" )
          bridge_status = Job::FINISHED;
      }
    }
  }
  
  //--------------------------------------------------------------------------
  // If the bridge status of the job is FINISHED, retrieve its results
  // from XtremWeb, and extract the output files required by the bridge
  //--------------------------------------------------------------------------
  if ( bridge_status == Job::FINISHED )
  {
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  Bridge job status = 'FINISHED'",
                  function_name, instance_name, bridge_job_id, xw_job_id);
    
    xw_command.xwcommand   = XW_RESULT;
    xw_command.xwjobid     = xw_job_id_str;
    xw_command.bridgejobid = bridge_job_id_str;
    returned_values        = xtremwebClient(xw_command);
    
    if ( returned_values.retcode != 0 )
    {
      LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb return code = x'%X' --> "
                    "%d  -->  3G Bridge status left unchanged",
                    function_name, instance_name, bridge_job_id, xw_job_id,
                    returned_values.retcode, returned_values.retcode / 256);
    }
    else
    {
      LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  Results successfully retrieved "
                    "from XtremWeb  '%s'",
                    function_name, instance_name, bridge_job_id, xw_job_id,
                    (returned_values.message).c_str());
    
#ifdef XW_SLEEP_BEFORE_DOWNLOAD
      // Sleep some time before download.  Should NOT be necessary.
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  Sleeping time before download = %d",
                     function_name, instance_name, bridge_job_id,
                     g_sleep_time_before_download);
      sleep(g_sleep_time_before_download);
#endif
      
      // Name of ZIP file contains XtremWeb job id after last '/'
      size_t pos_id = xw_job_id_str.rfind('/') + 1;
      LOG(LOG_DEBUG, "%s(%s)  xw_job_id = '%s'  pos_id=%d  xw_id_len=%d",
                     function_name, instance_name, xw_job_id, pos_id,
                     xw_job_id_str.length() - pos_id);
      string xw_zip_file_name = "*_ResultsOf_" +
       xw_job_id_str.substr(pos_id, xw_job_id_str.length() - pos_id) + ".zip";
      
      //----------------------------------------------------------------------
      // The XtremWeb ZIP file containing the job results should be in the
      // XtremWeb file folder.  Rename this ZIP file using the bridge job id.
      //----------------------------------------------------------------------
      string       zip_command_str;
      string       zip_params_str;
      const char * zip_command;
      const char * zip_params;
      
      zip_command_str = "/bin/mv";
      zip_params_str  = g_xw_files_folder + "/" + xw_zip_file_name +  " "  +
                        g_xw_files_folder + "/" + bridge_job_id + ".zip";
      zip_command     = zip_command_str.c_str();
      zip_params      = zip_params_str.c_str();
      LOG(LOG_DEBUG, "%s(%s)  command = '%s %s'",
                     function_name, instance_name, zip_command, zip_params);
      
      returned_values  = OutputAndErrorFromCommand(zip_command_str,
                                                   zip_params_str);
      returned_message = (returned_values.message).c_str();
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                     "command '%s %s'  Displayed message = '%s'",
                     function_name, instance_name, bridge_job_id,
                     returned_values.retcode, returned_values.retcode / 256,
                     zip_command, zip_params, returned_message);
      
      if ( returned_values.retcode == 0 )
      {
      //----------------------------------------------------------------------
      // Get the list of bridge ouptut files to be extracted from the XtremWeb
      // ZIP file to the bridge output files folder.
      // We suppose that all output files have to go to the same folder,
      // otherwise we would have to generate 1 unzip command per output file.
      //----------------------------------------------------------------------
      string output_file_path;
      
      zip_command_str = "/usr/bin/unzip";
      zip_params_str  = "-o " + g_xw_files_folder + "/" + bridge_job_id +
                        ".zip ";
      
      auto_ptr< vector<string> > output_file_names = job->getOutputs();
      for ( vector<string>::iterator file_iterator =
              output_file_names->begin();
            file_iterator != output_file_names->end();
            file_iterator++ )
      {
        zip_params_str   += *file_iterator + " " ;
        output_file_path  = job->getOutputPath(*file_iterator);
      }
      
      size_t pos_last_slash = output_file_path.rfind('/');
      zip_params_str += "-d " + output_file_path.substr(0, pos_last_slash);
      zip_command     = zip_command_str.c_str();
      zip_params      = zip_params_str.c_str();
      LOG(LOG_DEBUG, "%s(%s)  command = '%s %s'",
                     function_name, instance_name, zip_command, zip_params);
      
      //----------------------------------------------------------------------
      // Extract the bridge output files from the ZIP file containing results
      //----------------------------------------------------------------------
      returned_values  = OutputAndErrorFromCommand(zip_command_str,
                                                   zip_params_str);
      returned_message = (returned_values.message).c_str();
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                     "command '%s %s'  Displayed message = '%s'",
                     function_name, instance_name, bridge_job_id,
                     returned_values.retcode, returned_values.retcode / 256,
                     zip_command, zip_params, returned_message);
      }
      
      if ( returned_values.retcode != 0 )
      {
        LOG(LOG_ERR, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                     "command '%s %s'  Displayed message = '%s'",
                     function_name, instance_name, bridge_job_id,
                     returned_values.retcode, returned_values.retcode / 256,
                     zip_command, zip_params, returned_message);
        setJobStatusToError(function_name, instance_name, bridge_job_id, job);
      }
      
      logit_mon("event=job_status job_id=%s status=Finished", bridge_job_id);
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
