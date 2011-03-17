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
bool   g_xwclean_forced;              // Forced 'xwclean' before 'xwstatus'
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
//  @param config    Configuration file data
//  @param instance  Name of the plugin instance
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
  // xw_client_install_prefix
  //--------------------------------------------------------------------------
  char * xw_client_install_prefix = g_key_file_get_string(config, instance,
                                             "xwclient_install_prefix", NULL);
  
  //--------------------------------------------------------------------------
  // If 'xw_client_install_prefix' is given and seems correct, extract the
  // root folder and the client prefix from it.  Otherwise, use fixed values.
  //--------------------------------------------------------------------------
  string xw_client_install_prefix_str;
  size_t pos;
  string xw_root_str;
  string xw_client_prefix_str;
  
  if ( xw_client_install_prefix )
  {
    g_strstrip(xw_client_install_prefix);
    xw_client_install_prefix_str = xw_client_install_prefix;
    pos = xw_client_install_prefix_str.rfind('/');
  }
  
  if ( xw_client_install_prefix && (pos != 0) &&
       (pos < (xw_client_install_prefix_str.length() - 1)) )
  {
    xw_root_str          = xw_client_install_prefix_str.substr(0, pos - 1);
    xw_client_prefix_str = xw_client_install_prefix_str.substr(pos + 1);
  }
  else
  {
    xw_root_str          = "/opt";
    xw_client_prefix_str = "xwhep-client-";
  }
  
  const char * xw_root   = xw_root_str.c_str();
  LOG(LOG_DEBUG, "%s(%s)   XW client root folder:         '%s'     "
                 "XW client prefix: '%s'", function_name, instance_name,
                 xw_root, xw_client_prefix_str.c_str());
    
  //--------------------------------------------------------------------------
  //  Reading the directory 'xw_root_str', select the file beginning with
  //  'xw_client_prefix_str' and ended with the highest version number
  //--------------------------------------------------------------------------
  DIR *        directory_stream = opendir(xw_root);
  string       xw_client_folder_str;
  
  if ( ! directory_stream )
    throw new BackendException("XWHandler::XWHandler  can NOT read folder "
                               "'%s'", xw_root);
  else
  {
    dirent *     directory_entry;
    const char * file_name;
    string       file_name_str;
    size_t       xw_client_prefix_length = xw_client_prefix_str.length();
    size_t       pos1;
    size_t       pos2;
    string       number_str;
    int          major = 0;
    int          minor = 0;
    int          micro = 0;
    int          i, j, k;
    string       xw_file_name_str = "";
    
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
    
    xw_client_folder_str = xw_root_str + "/" + xw_file_name_str;
  }
  
  //--------------------------------------------------------------------------
  // Verify that 'xwclient_install_dir' is a readable folder.
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
  
  //--------------------------------------------------------------------------
  // Forced 'xwclean' before 'xwstatus'
  //--------------------------------------------------------------------------
  const char * xwclean_forced = g_key_file_get_string(config, instance,
                                                      "xwclean_forced", NULL);
  g_xwclean_forced = (xwclean_forced != 0) &&
                     (strcasecmp(xwclean_forced, "true") == 0);
  LOG(LOG_INFO, "%s(%s)  'xwclean' before 'xwstatus':   '%s'", function_name,
                instance_name, (g_xwclean_forced ? "true" : "false"));
  
  //--------------------------------------------------------------------------
  // xw_sleep_time_before_download
  //--------------------------------------------------------------------------
  const char * sleep_time_before_download = g_key_file_get_string(config,
                             instance, "xw_sleep_time_before_download", NULL);
  if ( sleep_time_before_download )
    g_sleep_time_before_download = atoi(sleep_time_before_download);
  else
    g_sleep_time_before_download = 0;
  LOG(LOG_INFO, "%s(%s)  Sleeping time before download: '%d'",
                function_name, instance_name, g_sleep_time_before_download);
  
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
//  Function  trimEol     
//  @param  my_string  Reference to a string
//  @return            void
//
//  This function removes ' \t\r\n' characters from the end of the string
//  given in parameter
//
//============================================================================
void trimEol(string & my_string)
{
  size_t pos_end = my_string.find_last_not_of(" \t\r\n");
  if      ( pos_end == string::npos )
    my_string.clear();
  else if ( pos_end + 1 < my_string.length() )
    my_string.erase(pos_end + 1);
}


//============================================================================
//
//  Function  outputAndErrorFromCommand     
//  @param arg_str_vector  Vector of strings containing the command to execute
//  @return                Struct containing return code and message displayed
//
//  This function executes the command given in parameter, retrieves the
//  message normally displayed by the command on STDOUT + STDERR, and returns
//  a structure containing the return code and this message.
//
//  Since it does NOT use any quoting, it accepts file names containing blanks
//  and is less vulnerable to unwanted injections of shell commands.
//
//============================================================================
returned_t outputAndErrorFromCommand(const auto_ptr< vector<string> > &
                                           arg_str_vector)
{
  const char * function_name = "XWHandler  outputAndErrorFromCommand";
  const char * instance_name = "";
  
  char         error_buffer[1024];
  returned_t   returned_values;
  
  size_t arg_str_vector_size = arg_str_vector->size();
  LOG(LOG_DEBUG, "%s(%s)  Command size = %d",
                 function_name, instance_name, arg_str_vector_size);
  
  if ( arg_str_vector_size < 1 )
  {
    sprintf(error_buffer, "Command size = %d  Empty comand is erroneous",
                          arg_str_vector_size);
    LOG(LOG_ERR, "%s(%s)  %s", function_name, instance_name, error_buffer);
    returned_values.retcode = -1;
    returned_values.message = error_buffer;
    return returned_values;
  }

  string command_str = (*arg_str_vector)[0];
  string args_str    = command_str;
  for ( vector<string>::iterator arg_iterator =
                                 (arg_str_vector->begin() + 1);
        arg_iterator != arg_str_vector->end();
        arg_iterator++ )
  { args_str += "' '" + *arg_iterator; }
  LOG(LOG_DEBUG, "%s(%s)  Command = '%s'",
                 function_name, instance_name, args_str.c_str());
  
  //--------------------------------------------------------------------------
  //  Create a pipe permitting to retrieve STDOUT and STDERR of child process
  //  Create child process to execute the command
  //--------------------------------------------------------------------------
  int pipe_std_out_err[2];
  
  pipe(pipe_std_out_err);
  
  pid_t process_id = fork();
  
  if ( process_id < 0)
  {
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
    //  Full path of the command
    //------------------------------------------------------------------------
    char * command = new char[command_str.length() + 1];
    strcpy(command, command_str.c_str());
    
    //------------------------------------------------------------------------
    //  Convert the vector of strings given in parameter into an array of
    //  character arrays to be passed to 'execv'
    //------------------------------------------------------------------------
    char * * args = new char*[arg_str_vector_size + 1];
    
    string command_name_str;
    size_t pos_last_slash = command_str.rfind('/');
    command_name_str = (pos_last_slash == string::npos) ?
                       command_str :
                       command_str.substr(pos_last_slash + 1);
    args[0] = new char[command_name_str.length() + 1];
    strcpy(args[0], command_name_str.c_str());
    
    for (size_t num = 1;  num < arg_str_vector_size;  num++)
    {
      args[num] = new char[(*arg_str_vector)[num].length() + 1];
      strcpy(args[num], (*arg_str_vector)[num].c_str());
    }
    args[arg_str_vector_size] = 0;
    
    args_str = string(args[0]);
    for (size_t num = 1;  args[num] != 0;  num++)
    { args_str += string("' '") + string(args[num]); }
    LOG(LOG_DEBUG, "%s(%s)  Child process:  execv('%s')  '%s'", function_name,
                   instance_name, command, args_str.c_str());
    
    //------------------------------------------------------------------------
    //  For command execution, call 'execv', which should NOT return
    //------------------------------------------------------------------------
    int return_code = execv(command, args);
    
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
                 function_name, instance_name, command_str.c_str(),
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
  trimEol(returned_values.message);
  LOG(LOG_DEBUG, "%s(%s)  Displayed message = '%s'", function_name,
                 instance_name, (returned_values.message).c_str());
  
  return returned_values;
}


//============================================================================
//
//  Function  simpleQuotesToDoubleQuotes
//  @param  input_str   Input string
//  @return             String with single quotes replaced by double quotes
//
//  This function returns the string given in parameter with simple quotes
//  replaced by double quotes.
//
//============================================================================
string simpleQuotesToDoubleQuotes(const string & input_str)
{
  string output_str    = input_str;
  size_t output_length = output_str.length();
  size_t pos_quote     = output_str.find("'");
  while ( pos_quote < output_length )
  {
    output_str[pos_quote] = '"';
    pos_quote++;
    if ( pos_quote < output_length )
      pos_quote = output_str.find("'", pos_quote);
  }
  return output_str;
}


//============================================================================
//
//  Function  logStringVectorToDebug    
//  @param  function_name   Name of the calling function
//  @param  instance_name   Name of the instance for the calling function
//  @param  bridge_job_id   ID of the current bridge job
//  @param  arg_str_vector  Vector of strings
//  @return                 NOTHING
//
//  This function logs a string vector to debug.
//
//============================================================================
void logStringVectorToDebug(const char *                       function_name,
                            const char *                       instance_name,
                            const char *                       bridge_job_id,
                            const auto_ptr< vector<string> > & arg_str_vector)
{
  string args_str = (*arg_str_vector)[0];
  
  for ( vector<string>::iterator arg_iterator =
                                 (arg_str_vector->begin() + 1);
        arg_iterator != arg_str_vector->end();
        arg_iterator++ )
  { args_str += "' '" + *arg_iterator; }
  
  LOG(LOG_DEBUG, "%s(%s)  Job '%s'  '%s'", function_name, instance_name,
                 bridge_job_id, args_str.c_str());
}


//============================================================================
//
//  Function  setJobStatusToError     
//  @param  function_name  Name of the calling function
//  @param  instance_name  Name of the instance for the calling function
//  @param  bridge_job_id  ID of the current bridge job
//  @param  job            Current bridge job
//  @param  message        Message to store for the job in the database
//  @return                NOTHING
//
//  This function logs a NOTICE message, sets the status of the bridge job
//  to ERROR, and stores the message for the job in the database.
//
//============================================================================
void setJobStatusToError(const char *   function_name,
                         const char *   instance_name,
                         const char *   bridge_job_id,
                         Job *          job,
                         const string & message)
{
  LOG(LOG_NOTICE, "%s(%s)  Job '%s'  Set status of bridge job to 'ERROR'",
                  function_name, instance_name, bridge_job_id);
  job->setStatus(Job::ERROR);
  job->setGridData(simpleQuotesToDoubleQuotes(message));
}


//============================================================================
//
//  Member function  submitJobs
//
//  @param jobs  Vector of pointers to bridge jobs to submit
//
//  This function submits a vector of bridge jobs to XtremWeb.
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
  bool          b_xw_data_error;
  auto_ptr< vector<string> > xw_env_str_vector(new vector<string>);
  auto_ptr< vector<string> > arg_str_vector(new vector<string>);
  auto_ptr< vector<string> > local_input_file_path_str_vector
                             (new vector<string>);
  auto_ptr< vector<string> > input_file_str_vector;
  string        input_file_name_str;
  string        input_file_path_str;
  const char *  input_file_name;
  const char *  input_file_path;
  FileRef       input_file_ref;
  const char *  input_file_md5_str;
  const char *  input_file_md5;
  int           input_file_size;
  string        xw_data_file_path_str;
  const char *  xw_data_file_path;
  FILE *        xw_data_file;
  char          xw_data_xml[1024];
  returned_t    returned_values;
  const char *  returned_message;
  string        xw_data_id_str;
  size_t        local_input_files_number;
  string        zip_file_name_str;
  const char *  zip_file_name;
  string        job_batch_id_str;
  string        xw_group_id_str;
  size_t        pos_xw_id;
  string        xw_job_id_str;
  const char *  xw_job_id;
  
  //------------------------------------------------------------------------
  //  Loop on jobs to be submitted
  //------------------------------------------------------------------------
  for ( JobVector::iterator job_iterator = jobs.begin();
        job_iterator != jobs.end();
        job_iterator++ )
  {
    job             = *job_iterator;
    bridge_job_id   = (job->getId()).c_str();
    
    b_xw_data_error = false;
    xw_env_str_vector->clear();
    
    //------------------------------------------------------------------------
    //  Retrieve list of input files
    //------------------------------------------------------------------------
    arg_str_vector->clear();
    arg_str_vector->reserve(3);
    arg_str_vector->push_back(g_xw_client_bin_folder + "xwsenddata");
    arg_str_vector->push_back(string("--xwxml"));
    arg_str_vector->push_back(string(""));
    local_input_file_path_str_vector->clear();
    
    input_file_str_vector = job->getInputs();
    LOG(LOG_DEBUG, "%s(%s)  Job '%s'  Number of input files = %d",
                   function_name, instance_name, bridge_job_id,
                   input_file_str_vector->size());
    
    for ( vector<string>::iterator input_file_iterator =
                                   input_file_str_vector->begin();
          input_file_iterator !=   input_file_str_vector->end();
          input_file_iterator++ )
    {
      input_file_name_str = *input_file_iterator;
      input_file_path_str = job->getInputPath(input_file_name_str);
      input_file_name     = input_file_name_str.c_str();
      input_file_path     = input_file_path_str.c_str();
      
      if ( input_file_path_str[0] == '/' )
      //----------------------------------------------------------------------
      //  Files beginning with '/' are local files
      //----------------------------------------------------------------------
      {
        LOG(LOG_DEBUG, "%s(%s)  Job '%s'  input_file_name = '%s'  "
                       "input_file_path = '%s'", function_name, instance_name,
                       bridge_job_id, input_file_name, input_file_path);
        local_input_file_path_str_vector->push_back(input_file_path_str);
      }
      else
      //----------------------------------------------------------------------
      //  URLS have to be specified to XtremWeb as data
      //----------------------------------------------------------------------
      {
        //  Following 2 lines work only if URL = schema://server/basename
        //  xw_env_str_vector.push_back(string("--xwenv"));
        //  xw_env_str_vector.push_back(input_file_path_str);
        
        //--------------------------------------------------------------------
        //  Get MD5 and size for the URL
        //--------------------------------------------------------------------
        input_file_ref        = job->getInputRef(input_file_name_str);
        input_file_md5_str    = input_file_ref.getMD5();
        input_file_size       = input_file_ref.getSize();
        input_file_md5        = input_file_md5_str ? input_file_md5_str : "";
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
        //  Create the XML file describing XtremWeb data
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
        //  Send the data to XtremWeb
        //--------------------------------------------------------------------
        arg_str_vector->back() = xw_data_file_path_str;
        logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                               arg_str_vector);
        
        returned_values  = outputAndErrorFromCommand(arg_str_vector);
        returned_message = (returned_values.message).c_str();
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
        //  From the message displayed by XtremWeb, extract the DATA id
        //--------------------------------------------------------------------
        pos_xw_id = (returned_values.message).find("xw://");
        if ( pos_xw_id == string::npos )
        {
          LOG(LOG_NOTICE, "%s(%s)  Job '%s'  'xw://' NOT found inside "
                          "message displayed by XtremWeb",
                          function_name, instance_name, bridge_job_id);
          b_xw_data_error = true;
          break;
        }
        
        xw_data_id_str = (returned_values.message).substr(pos_xw_id,
                              (returned_values.message).length() - pos_xw_id);
        trimEol(xw_data_id_str);
        xw_env_str_vector->push_back(string("--xwenv"));
        xw_env_str_vector->push_back(xw_data_id_str);
      }
    }
    
    if ( b_xw_data_error )
    {
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          returned_values.message);
      break;
    }
    
    //------------------------------------------------------------------------
    //  If there is exactly 1 local input file, pass it as parameter of
    //  '--xwenv'
    //------------------------------------------------------------------------
    local_input_files_number = local_input_file_path_str_vector->size();
    if ( local_input_files_number == 1 )
    {
      xw_env_str_vector->push_back(string("--xwenv"));
      xw_env_str_vector->push_back((*local_input_file_path_str_vector)[0]);
    }
    
    //------------------------------------------------------------------------
    //  If there is more than 1 local input file, store them all inside a ZIP
    //------------------------------------------------------------------------
    else if ( local_input_files_number >  1 )
    {
      zip_file_name_str = g_xw_files_folder + "/" + bridge_job_id + ".zip";
      zip_file_name     = zip_file_name_str.c_str();
      arg_str_vector->clear();
      arg_str_vector->reserve(4 + local_input_files_number);
      arg_str_vector->push_back(string("/usr/bin/zip"));
      arg_str_vector->push_back(string("-v"));
      arg_str_vector->push_back(string("-j"));
      arg_str_vector->push_back(zip_file_name_str);
      arg_str_vector->insert(arg_str_vector->end(),
                             local_input_file_path_str_vector->begin(),
                             local_input_file_path_str_vector->end());
      logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                             arg_str_vector);
      
      returned_values  = outputAndErrorFromCommand(arg_str_vector);
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
        setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                            returned_values.message);
        break;
      }
      
      xw_env_str_vector->push_back(string("--xwenv"));
      xw_env_str_vector->push_back(zip_file_name_str);
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
      xw_group_id_str = job_batch_id_str;
      xw_env_str_vector->push_back(string("--xwgroup"));
      xw_env_str_vector->push_back(xw_group_id_str);
    }
    
    //------------------------------------------------------------------------
    // Submit the job to XtremWeb
    //------------------------------------------------------------------------
    arg_str_vector->clear();
    arg_str_vector->reserve(3 + xw_env_str_vector->size());
    arg_str_vector->push_back(g_xw_client_bin_folder + "xwsubmit");
    arg_str_vector->push_back(job->getName());
    arg_str_vector->push_back(job->getArgs());
    arg_str_vector->insert(arg_str_vector->end(), xw_env_str_vector->begin(),
                                                  xw_env_str_vector->end());
    logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                           arg_str_vector);
    
    returned_values  = outputAndErrorFromCommand(arg_str_vector);
    returned_message = (returned_values.message).c_str();
    
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
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          returned_values.message);
      break;
    }
    
    //------------------------------------------------------------------------
    // From the message displayed by XtremWeb, extract the XtremWeb job id
    //------------------------------------------------------------------------
    pos_xw_id = (returned_values.message).find("xw://");
    if ( pos_xw_id == string::npos )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s'  'xw://' NOT found inside message "
                      "displayed by XtremWeb",
                      function_name, instance_name, bridge_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          returned_values.message);
      break;
    }
    
    xw_job_id_str = (returned_values.message).substr(pos_xw_id,
                              (returned_values.message).length() - pos_xw_id);
    trimEol(xw_job_id_str);
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
//  This function requests DBHandler::pollJobs() to update the status of all
//  jobs whose current bridge status is RUNNING or CANCEL.
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
//  Member function  updateJobStatus
//
//  @param job            Pointer to the bridge job to update
//  @param xw_job_status  XtremWeb status of the job
//  @param xw_message     XtremWeb message associated to the job
//
//  This function updates the status of a given bridge job :
//  - From the XtremWeb status of the job, calculate the new bridge status.
//  - Store the new bridge status and the XtremWeb message in the database.
//    ATTENTION: For DB, simple quotes must be changed to something else.
//
//============================================================================
void XWHandler::updateJobStatus(Job *          job,
                                const string & xw_job_current_status,
                                const string & xw_message)
{
  const char * function_name = "XWHandler::updateJobStatus";
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
  
  const char *         bridge_job_id         = (job->getId()).c_str();
  const char *         xw_job_id             = (job->getGridId()).c_str();
  const char *         xw_job_status         = xw_job_current_status.c_str();
  const Job::JobStatus bridge_job_old_status = job->getStatus();
  
  LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  XtremWeb job status = '%s'  "
                 "Bridge job old status = %d has perhaps to be updated",
                 function_name, instance_name, bridge_job_id, xw_job_id,
                 xw_job_status, bridge_job_old_status);
  
  for ( unsigned xw_status_no = 0;
        xw_to_bridge_status_relation[xw_status_no].xw_job_status != "";
        xw_status_no++ )
  {
    if ( xw_to_bridge_status_relation[xw_status_no].xw_job_status ==
         xw_job_current_status )
    {
      Job::JobStatus bridge_job_new_status =
                 xw_to_bridge_status_relation[xw_status_no].bridge_job_status;
      
      if ( bridge_job_new_status != bridge_job_old_status )
      {
        string xw_date_str = "";
        
        //--------------------------------------------------------------------
        //  Try to extract the completion date of the XtremWeb job.
        //  Initial value of the 'xw_date_str' variable is the empty string.
        //--------------------------------------------------------------------
        if ( (xw_job_current_status == "COMPLETED") ||
             (xw_job_current_status == "ERROR")     ||
             (xw_job_current_status == "ABORTED")   )
        {
          const char * date_header = "COMPLETEDDATE='";
          size_t pos_date          = xw_message.find(date_header);
          if ( pos_date != string::npos )
          {
            pos_date += strlen(date_header);
            size_t pos_quote = xw_message.find("'", pos_date);
            if ( pos_quote != string::npos )
              xw_date_str = xw_message.substr(pos_date, pos_quote - pos_date);
          }
        }
        
        LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb job status = '%s'  "
                      "Bridge job new status = %d  Completed date = '%s'",
                      function_name, instance_name, bridge_job_id, xw_job_id,
                      xw_job_status, bridge_job_new_status,
                      xw_date_str.c_str());
        job->setStatus(bridge_job_new_status);
        job->setGridData(simpleQuotesToDoubleQuotes(xw_message));
        
        LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  XtremWeb job status = '%s'  "
                       "Bridge job new status = %d successfully stored",
                       function_name, instance_name, bridge_job_id, xw_job_id,
                       xw_job_status, bridge_job_new_status);
      }
      break;
    }
  }
  
}


//============================================================================
//
//  Member function  poll
//
//  @param job  Pointer to the bridge job to poll
//
//  This function polls the status of a given bridge job :
//  It is used as a callback for DBHandler::pollJobs().
//  Depending on the bridge status of the job :
//  - CANCEL :
//    Destroy the corresponding XtremWeb job.
//  - RUNNING :
//    Query XtremWeb for the status of the corresponding XtremWeb job.
//    If the XtremWeb job is COMPLETED, retrieve the XtremWeb results.
//    Update the status of the bridge job accordingly.
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

  auto_ptr< vector<string> > arg_str_vector(new vector<string>);
  returned_t    returned_values;
  const char *  returned_message;
  unsigned char bridge_status = job->getStatus();
  
  //--------------------------------------------------------------------------
  //  If the bridge status of the job is CANCEL, delete it from XtremWeb
  //--------------------------------------------------------------------------
  if ( bridge_status == Job::CANCEL )
  {
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  Bridge job status = 'CANCEL'",
                  function_name, instance_name, bridge_job_id, xw_job_id);
    
    arg_str_vector->clear();
    arg_str_vector->reserve(2);
    arg_str_vector->push_back(g_xw_client_bin_folder + "xwrm");
    arg_str_vector->push_back(xw_job_id_str);
    logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                           arg_str_vector);
    
    returned_values = outputAndErrorFromCommand(arg_str_vector);
    
    //------------------------------------------------------------------------
    //  Only if cancellation is successful, update the bridge job status
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
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          returned_values.message);
    }
  }
  
  //--------------------------------------------------------------------------
  //  If the bridge status of the job is RUNNING, refresh it from XtremWeb
  //--------------------------------------------------------------------------
  if ( bridge_status == Job::RUNNING )
  {
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  Bridge job status = 'RUNNING'",
                  function_name, instance_name, bridge_job_id, xw_job_id);
    
    //------------------------------------------------------------------------
    //  Force XtremWeb to refresh its cache.
    //  This should NOT be necessary anymore.
    //------------------------------------------------------------------------
    if ( g_xwclean_forced )
    {
      arg_str_vector->clear();
      arg_str_vector->push_back(g_xw_client_bin_folder + "xwclean");
      logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                             arg_str_vector);
      returned_values = outputAndErrorFromCommand(arg_str_vector);
    }
    
    //------------------------------------------------------------------------
    //  Query job status from XtremWeb
    //------------------------------------------------------------------------
    arg_str_vector->clear();
    arg_str_vector->reserve(2);
    arg_str_vector->push_back(g_xw_client_bin_folder + "xwstatus");
    arg_str_vector->push_back(xw_job_id_str);
    logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                           arg_str_vector);
    
    returned_values = outputAndErrorFromCommand(arg_str_vector);
    
    //------------------------------------------------------------------------
    //  Try to parse the message displayed by XtremWeb
    //------------------------------------------------------------------------
    if ( (returned_values.message).find("ERROR : object not found") !=
         string::npos )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  NOT found by XtremWeb",
                      function_name, instance_name, bridge_job_id, xw_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          returned_values.message);
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
      // Initial value of the 'xw_job_status_str' variable is the empty string
      string xw_job_status_str = "";
      
      //----------------------------------------------------------------------
      //  Message displayed by XtremWeb should contain:  STATUS='<status>'
      //----------------------------------------------------------------------
      const char * status_header = "STATUS='";
      string       xw_message    = returned_values.message;
      size_t       pos_status    = xw_message.find(status_header);
      size_t       pos_quote     = 0;
      if ( pos_status != string::npos )
      {
        pos_status += strlen(status_header);
        pos_quote   = xw_message.find("'", pos_status);
        if ( pos_quote != string::npos )
          xw_job_status_str = xw_message.substr(pos_status,
                                                pos_quote - pos_status );
      }
      
      //----------------------------------------------------------------------
      //  If relevant, use XtremWeb job status to update bridge job status
      //----------------------------------------------------------------------
      if ( xw_job_status_str == "" )
      {
        LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb status NOT found -->"
                      "3G Bridge status left unchanged",
                      function_name, instance_name, bridge_job_id, xw_job_id);
      }
      else
      {
        //--------------------------------------------------------------------
        //  Store the xtremWeb message inside the 'griddata' attribute of the
        //  bridge job
        //--------------------------------------------------------------------
        xw_message = xw_message.substr(xw_message.find_first_not_of(' '));
        LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb job status = '%s'",
                      function_name, instance_name, bridge_job_id,
                      xw_job_id, xw_job_status_str.c_str());
      
        updateJobStatus(job, xw_job_status_str, xw_message);
      
        //--------------------------------------------------------------------
        //  If the XtremWeb status of the job is COMPLETED,
        //    set its bridge status to FINISHED
        //--------------------------------------------------------------------
        if ( xw_job_status_str == "COMPLETED" )
          bridge_status = Job::FINISHED;
      }
    }
  }
  
  //--------------------------------------------------------------------------
  //  If the bridge status of the job is FINISHED, retrieve its results
  //  from XtremWeb, and extract the output files required by the bridge
  //--------------------------------------------------------------------------
  if ( bridge_status == Job::FINISHED )
  {
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  Bridge job status = 'FINISHED'",
                  function_name, instance_name, bridge_job_id, xw_job_id);
    
    //------------------------------------------------------------------------
    //  If the current folder is not the folder for XtremWeb output files,
    //  go there.
    //------------------------------------------------------------------------
    char * cwd = getcwd((char*)NULL, 0);
    LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  cwd = '%s'", function_name, 
                  instance_name, bridge_job_id, xw_job_id, cwd);
    
    const char * workdir = g_xw_files_folder.c_str();
    if  ( strcmp(cwd, workdir) != 0 )
    {
      LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  Executing chdir('%s')",
                     function_name, instance_name, bridge_job_id, xw_job_id,
                     workdir);
      chdir(workdir);
    }
    
    free(cwd);               // 'cwd' has been dynamically allocated by getcwd
    
    //------------------------------------------------------------------------
    //  Retrieve the results of the XtremWeb job
    //------------------------------------------------------------------------
    arg_str_vector->clear();
    arg_str_vector->reserve(2);
    arg_str_vector->push_back(g_xw_client_bin_folder + "xwresults");
    arg_str_vector->push_back(xw_job_id_str);
    logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                           arg_str_vector);
    
    returned_values = outputAndErrorFromCommand(arg_str_vector);
    
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
    
      //  Sleep some time before download.  Should NOT be necessary.
      if ( g_sleep_time_before_download > 0 )
      {
        LOG(LOG_DEBUG, "%s(%s)  Job '%s'  Sleeping time before download = %d",
                       function_name, instance_name, bridge_job_id,
                       g_sleep_time_before_download);
        sleep(g_sleep_time_before_download);
      }
      
      //  Name of ZIP file contains XtremWeb job id after last '/'
      size_t pos_id = xw_job_id_str.rfind('/') + 1;
      LOG(LOG_DEBUG, "%s(%s)  xw_job_id = '%s'  pos_id=%d  xw_id_len=%d",
                     function_name, instance_name, xw_job_id, pos_id,
                     xw_job_id_str.length() - pos_id);
      
      string xw_job_uid_str   = xw_job_id_str.substr(pos_id,
                                             xw_job_id_str.length() - pos_id);
      string xw_zip_file_name_str = "*_ResultsOf_" + xw_job_uid_str + ".zip";
      string xw_zip_file_path_str = g_xw_files_folder +
                                    "/ResultsOf_" + xw_job_uid_str + ".zip";
      
      //----------------------------------------------------------------------
      //  The XtremWeb ZIP file containing the job results should be in the
      //  XtremWeb file folder.  Rename this ZIP file with a shorter name.
      //----------------------------------------------------------------------
      arg_str_vector->clear();
      arg_str_vector->reserve(3);
      arg_str_vector->push_back(string("/bin/sh"));
      arg_str_vector->push_back(string("-c"));
      arg_str_vector->push_back(string("/bin/mv  ") + g_xw_files_folder +
                                "/"  + xw_zip_file_name_str +
                                "  " + xw_zip_file_path_str);
      logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                             arg_str_vector);
      
      returned_values  = outputAndErrorFromCommand(arg_str_vector);
      returned_message = (returned_values.message).c_str();
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                     "command '%s'  Displayed message = '%s'",
                     function_name, instance_name, bridge_job_id,
                     returned_values.retcode, returned_values.retcode / 256,
                     "/bin/mv", returned_message);
      
      if ( returned_values.retcode == 0 )
      {
      //----------------------------------------------------------------------
      //  Get the list of bridge ouptut files to be extracted from the
      //  XtremWeb ZIP file to the bridge output files folder.
      //  We suppose that all output files have to go to the same folder,
      //  otherwise we would have to generate 1 unzip command per output file.
      //----------------------------------------------------------------------
      auto_ptr< vector<string> > output_file_names = job->getOutputs();
      arg_str_vector->clear();
      arg_str_vector->reserve(5 + output_file_names->size());
      arg_str_vector->push_back(string("/usr/bin/unzip"));
      arg_str_vector->push_back(string("-o"));
      arg_str_vector->push_back(xw_zip_file_path_str);
      arg_str_vector->insert(arg_str_vector->end(),
                             output_file_names->begin(),
                             output_file_names->end());
      arg_str_vector->push_back(string("-d"));
      string output_file_path = job->getOutputPath(output_file_names->back());
      size_t pos_last_slash   = output_file_path.rfind('/');
      arg_str_vector->push_back(output_file_path.substr(0, pos_last_slash));
      
      logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                             arg_str_vector);
      
      //----------------------------------------------------------------------
      //  Extract the bridge output files from the ZIP file containing results
      //----------------------------------------------------------------------
      returned_values  = outputAndErrorFromCommand(arg_str_vector);
      returned_message = (returned_values.message).c_str();
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                     "command '%s'  Displayed message = '%s'",
                     function_name, instance_name, bridge_job_id,
                     returned_values.retcode, returned_values.retcode / 256,
                     "/usr/bin/unzip", returned_message);
      }
      
      if ( returned_values.retcode != 0 )
      {
        LOG(LOG_ERR, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                     "command '%s'  Displayed message = '%s'",
                     function_name, instance_name, bridge_job_id,
                     returned_values.retcode, returned_values.retcode / 256,
                     "/usr/bin/unzip", returned_message);
        setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                            returned_values.message);
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
