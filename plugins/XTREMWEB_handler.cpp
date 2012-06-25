/*
 * Copyright (C) 2011-2012 LAL, Univ Paris-Sud, IN2P3/CNRS  (Etienne URBAH)
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
#include "DLException.h"
#include "LogMonMsg.h"

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
#include <netdb.h>
#include <openssl/ssl.h>


using namespace std;

struct returned_t
{
  int    retcode;
  string message;
};


//============================================================================
//  Global variables.  In fact, they should probably be private attributes.
//============================================================================
char *    g_xw_https_server;            // XtremWeb-HEP HTTPS server
char *    g_xw_https_port;              // XtremWeb-HEP HTTPS port
char *    g_xw_user;                    // XtremWeb-HEP user login
char *    g_xw_password;                // XtremWeb-HEP user password
int       g_xw_socket_fd;               // Socket to XtremWeb-HEP server
SSL_CTX * g_ssl_context;                // SSL context
SSL *     g_ssl_xw;                     // SSL object for XtremWeb-HEP server
string    g_xw_client_bin_folder_str;   // XtremWeb-HEP client binaries folder
string    g_xw_files_folder_str;        // Folder for XW input + output files
char *    g_requested_lang;             // Language for localized messages
bool      g_xwclean_forced;             // Forced 'xwclean' before 'xwstatus'
int       g_sleep_time_before_download; // Sleeping time before download in s
string    g_xw_apps_message_str;        // XW message listing XW applications


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
  
  char         error_buffer[BUFSIZ];
  returned_t   returned_values;
  
  size_t arg_str_vector_size = arg_str_vector->size();
  LOG(LOG_DEBUG, "%s(%s)  Command size = %zd",
                 function_name, instance_name, arg_str_vector_size);
  
  if ( arg_str_vector_size < 1 )
  {
    sprintf(error_buffer, "Command size = %zd  Empty comand is erroneous",
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
    sprintf(error_buffer, "fork() return_code = %d  errno = %d  '%s'",
                          process_id, errno, sys_errlist[errno]);
    LOG(LOG_ERR, "%s(%s)  %s", function_name, instance_name, error_buffer);
    returned_values.retcode = process_id;
    returned_values.message = error_buffer;
    return returned_values;
  }
  
  if ( process_id == 0)
  {
    //------------------------------------------------------------------------
    //  Current process is the child.  Close pipe input.
    //  Just in case LOG writes to STDOUT, redirection of child STDOUT and
    //  STDERR to the pipe output must be performed just before 'execv'.
    //------------------------------------------------------------------------
    close(pipe_std_out_err[0]);
    
    //------------------------------------------------------------------------
    //  If necessary, set the LANG environment variable to the requested value
    //------------------------------------------------------------------------
    if ( g_requested_lang )
      setenv("LANG", g_requested_lang, 1);
    
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
    //  Redirect child STDOUT and STDERR to the pipe output
    //------------------------------------------------------------------------
    dup2(pipe_std_out_err[1], STDOUT_FILENO);
    dup2(pipe_std_out_err[1], STDERR_FILENO);
    
    //------------------------------------------------------------------------
    //  For command execution, call 'execv', which should NOT return
    //------------------------------------------------------------------------
    int return_code = execv(command, args);
    
    LOG(LOG_ERR, "%s(%s)  Child process:  execv return_code = %d  errno = %d "
                 "  '%s'", function_name, instance_name, return_code, errno,
                 sys_errlist[errno]);
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
  
  LOG(LOG_NOTICE, "%s(%s)  Plugin creation for instance   '%s'",
                  function_name, instance_name, instance);
  
  groupByNames = false;
  
  //==========================================================================
  //  xw_https_server, xw_https_port, xw_user, xw_password
  //==========================================================================
  g_xw_https_server = g_key_file_get_string(config, instance,
                                            "xw_https_server", NULL);
  g_xw_https_port   = g_key_file_get_string(config, instance,
                                            "xw_https_port",   NULL);
  g_xw_user         = g_key_file_get_string(config, instance,
                                            "xw_user",         NULL);
  g_xw_password     = g_key_file_get_string(config, instance,
                                            "xw_password",     NULL);
  
  if ( g_xw_https_server && g_xw_https_port && g_xw_user && g_xw_password )
  {
    g_strstrip(g_xw_https_server);
    g_strstrip(g_xw_https_port);
    g_strstrip(g_xw_user);
    g_strstrip(g_xw_password);
    LOG(LOG_NOTICE, "%s(%s)  XW HTTPS server and port:      '%s:%s'     "
                    "XW user: '%s'", function_name, instance_name,
                    g_xw_https_server, g_xw_https_port, g_xw_user);
    
    //------------------------------------------------------------------------
    //  Get protocol number for TCP
    //------------------------------------------------------------------------
    const char * proto_tcp_name = "tcp";
    LOG(LOG_DEBUG, "%s(%s)  Get protocol entry for '%s'",
                   function_name, instance_name, proto_tcp_name);
    struct protoent * protoent_tcp = getprotobyname(proto_tcp_name);
    if ( ! protoent_tcp )
      LOG(LOG_ERR, "%s(%s)  NO protocol entry found for '%s'",
                   function_name, instance_name, proto_tcp_name);
    else
    {
      //----------------------------------------------------------------------
      //  Get a list of address structures suitable for TCP connection to 
      //  XtremWeb-HEP server name and port
      //----------------------------------------------------------------------
      struct addrinfo   addrinfo_hints;
      struct addrinfo * first_addrinfo_entry;
      struct addrinfo * addrinfo_entry;
      
      LOG(LOG_DEBUG, "%s(%s)  Get address info for '%s' connection to "
                     "'%s:%s'", function_name, instance_name, proto_tcp_name,
                     g_xw_https_server, g_xw_https_port);
      
      memset(&addrinfo_hints, 0, sizeof(struct addrinfo));
      addrinfo_hints.ai_family   = AF_UNSPEC;            // Allow IPv4 or IPv6
      addrinfo_hints.ai_socktype = SOCK_STREAM;
      addrinfo_hints.ai_flags    = 0;
      addrinfo_hints.ai_protocol = protoent_tcp->p_proto;
      int return_code = getaddrinfo(g_xw_https_server, g_xw_https_port,
                                    &addrinfo_hints, &first_addrinfo_entry);
      
      if ( return_code != 0)
        LOG(LOG_ERR, "%s(%s)  NO address info for '%s' connection to '%s:%s'",
                     function_name, instance_name, proto_tcp_name,
                     g_xw_https_server, g_xw_https_port);
      else
      {
        //--------------------------------------------------------------------
        //  Try each address until we successfully connect().
        //  If socket() (or connect()) fails, we (close the socket and) try
        //  the next address.
        //--------------------------------------------------------------------
        LOG(LOG_DEBUG, "%s(%s)  Trying '%s' connection to XW server '%s:%s'",
                       function_name, instance_name, proto_tcp_name,
                       g_xw_https_server, g_xw_https_port);
        
        for ( addrinfo_entry =  first_addrinfo_entry;
              addrinfo_entry != NULL;
              addrinfo_entry =  addrinfo_entry->ai_next )
        {
          g_xw_socket_fd = socket(addrinfo_entry->ai_family,
                                  addrinfo_entry->ai_socktype,
                                  addrinfo_entry->ai_protocol);
          if ( g_xw_socket_fd == -1 )
              continue;
          
          if ( connect(g_xw_socket_fd, addrinfo_entry->ai_addr,
                                       addrinfo_entry->ai_addrlen) == 0 )
              break;                                                // Success
          
          close(g_xw_socket_fd);
        }
        
        freeaddrinfo(first_addrinfo_entry);                // No longer needed
        
        if ( addrinfo_entry == NULL )
          LOG(LOG_ERR, "%s(%s)  '%s' connection to XW server '%s:%s' FAILED",
                       function_name, instance_name, proto_tcp_name,
                       g_xw_https_server, g_xw_https_port);
        else
        {
          LOG(LOG_INFO, "%s(%s)  '%s' connection to XW server '%s:%s' "
                        "succeeded", function_name, instance_name,
                        proto_tcp_name, g_xw_https_server, g_xw_https_port);
          
          //------------------------------------------------------------------
          //  Initialize SSL
          //------------------------------------------------------------------
          LOG(LOG_DEBUG, "%s(%s)  Creation of SSL context with "
                         "'SSLv23_client_method'",
                         function_name, instance_name);
          
          SSL_load_error_strings();
          SSL_library_init();
          
          g_ssl_context = SSL_CTX_new(SSLv23_client_method());
          if  ( ! g_ssl_context )
            LOG(LOG_ERR, "%s(%s)  Creation of SSL context with "
                         "'SSLv23_client_method' FAILED",
                         function_name, instance_name);
          else
          {
            LOG(LOG_DEBUG, "%s(%s)  Creation of SSL object",
                           function_name, instance_name);
            
            SSL_CTX_set_verify(g_ssl_context, SSL_VERIFY_NONE, NULL);
            
            g_ssl_xw = SSL_new(g_ssl_context);
            if  ( ! g_ssl_xw )
              LOG(LOG_ERR, "%s(%s)  Creation of SSL object FAILED",
                           function_name, instance_name);
            else
            {
              LOG(LOG_DEBUG, "%s(%s)  Trying to connect the SSL object to "
                             "the socket for the XW server at '%s:%s'",
                             function_name, instance_name,
                             g_xw_https_server, g_xw_https_port);
              
              //--------------------------------------------------------------
              //  Connect the SSL object to the XtremWeb-HEP socket, and
              //  initiate the TLS/SSL handshake with the XtremWeb-HEP server
              //--------------------------------------------------------------
              if ( SSL_set_fd(g_ssl_xw, g_xw_socket_fd) != 1 )
                LOG(LOG_ERR, "%s(%s)  Connection of the SSL object to the "
                             "socket for the XW server at '%s:%s' FAILED",
                             function_name, instance_name,
                             g_xw_https_server, g_xw_https_port);
              else
              {
                LOG(LOG_INFO, "%s(%s)  Connection of the SSL object to the "
                              "socket for the XW server at '%s:%s' succeeded",
                              function_name, instance_name,
                              g_xw_https_server, g_xw_https_port);
                
                LOG(LOG_DEBUG, "%s(%s)  Trying TLS/SSL handshake with the XW "
                               "server at '%s:%s'",
                               function_name, instance_name,
                               g_xw_https_server, g_xw_https_port);
                
                return_code = SSL_connect(g_ssl_xw);
                if ( return_code != 1 )
                  LOG(LOG_ERR, "%s(%s)  TLS/SSL handshake with the XW server "
                               "at '%s:%s' FAILED   SSL return code = %d",
                               function_name, instance_name,
                               g_xw_https_server, g_xw_https_port,
                               SSL_get_error(g_ssl_xw, return_code));
                else
                {
                  LOG(LOG_INFO, "%s(%s)  TLS/SSL handshake with the XW "
                                "server at '%s:%s' succeeded     "
                                "SSL verify result = %ld",
                                function_name, instance_name,
                                g_xw_https_server, g_xw_https_port,
                                SSL_get_verify_result(g_ssl_xw));
                  
                  //------------------------------------------------------------
                  //  Get XtremWeb-HEP version
                  //------------------------------------------------------------
                  const string xw_version_request_str =
                               "GET /?xwcommand=%3Cversion%3E"
                               "%3Cuser%20login=%22" + string(g_xw_user) +
                               "%22%20password=%22" + string(g_xw_password) +
                               "%22/%3E%3C/version%3E HTTP/1.0\n"
                               "User-Agent: 3G-Bridge_XtremWeb-HEP_plugin\n"
                               "Host: " + string(g_xw_https_server) + ":" +
                               string(g_xw_https_port) + "\n\n";
                  const char * xw_version_request =
                                               xw_version_request_str.c_str();
                  
                  LOG(LOG_INFO, "%s(%s)  '%s:%s'  Request '%s'",
                                function_name, instance_name,
                                g_xw_https_server, g_xw_https_port,
                                xw_version_request);
                
                  int req_len = xw_version_request_str.length();
                  if ( SSL_write(g_ssl_xw, xw_version_request, req_len) !=
                       req_len )
                    LOG(LOG_ERR, "%s(%s)  '%s:%s' Request 'version' FAILED",
                                 function_name, instance_name,
                                 g_xw_https_server, g_xw_https_port);
                  else
                  {
                    size_t  http_response_len = 0;
                    string  http_response_str = "";
                    char    http_response_buf[BUFSIZ];
                    ssize_t nread = BUFSIZ;
                    while ( nread == BUFSIZ )
                    {
                      nread = SSL_read(g_ssl_xw, http_response_buf, BUFSIZ);
                      http_response_len += nread;
                      http_response_str += http_response_buf;
                    }
                    size_t pos_length =
                                    http_response_str.find("Content-Length:");
                    size_t xw_version_response_len =
                      atoi((http_response_str.substr(pos_length+15)).c_str());
                    LOG(LOG_DEBUG, "%s(%s)  XW response for version number "
                                   "has %zd bytes", function_name,
                                   instance_name,  xw_version_response_len);
                    
                    http_response_len = 0;
                    http_response_str = "";
                    while ( http_response_len < xw_version_response_len )
                    {
                      nread = SSL_read(g_ssl_xw, http_response_buf,
                                       xw_version_response_len);
                      http_response_len += nread;
                      http_response_str += http_response_buf;
                    }
                    trimEol(http_response_str);
                    LOG(LOG_NOTICE, "%s(%s)  XW version number:             "
                                    "'%s'", function_name, instance_name,
                                    http_response_str.c_str());
                  }
                }
              }
              SSL_free(g_ssl_xw);
            }
            SSL_CTX_free(g_ssl_context);
          }
          close(g_xw_socket_fd);
        }
      }
    }
  }
  
  //==========================================================================
  //  xw_client_install_prefix
  //==========================================================================
  char * xw_client_install_prefix = g_key_file_get_string(config, instance,
                                            "xw_client_install_prefix", NULL);
  
  //--------------------------------------------------------------------------
  //  If 'xw_client_install_prefix' is given and seems correct, extract the
  //  root folder and the client prefix from it.  Otherwise, use fixed values.
  //--------------------------------------------------------------------------
  g_xw_client_bin_folder_str = "/usr/bin/";
  DIR *  directory_stream;
  
  if ( xw_client_install_prefix )
  {
    g_strstrip(xw_client_install_prefix);
    LOG(LOG_INFO, "%s(%s)   XW client install prefix:      '%s'",
                  function_name, instance_name, xw_client_install_prefix);
    string xw_client_install_prefix_str = xw_client_install_prefix;
    size_t pos = xw_client_install_prefix_str.rfind('/');
    
    if ( (pos != 0) && (pos < (xw_client_install_prefix_str.length() - 1)) )
    {
      string xw_root_str = xw_client_install_prefix_str.substr(0, pos);
      string xw_client_prefix_str =
                           xw_client_install_prefix_str.substr(pos + 1);
      
      const char * xw_root   = xw_root_str.c_str();
      LOG(LOG_INFO, "%s(%s)   XW client root folder:         '%s'     "
                    "XW client prefix: '%s'", function_name, instance_name,
                    xw_root, xw_client_prefix_str.c_str());
        
      //----------------------------------------------------------------------
      //  Reading the directory 'xw_root_str', select the file beginning with
      //  'xw_client_prefix_str' and ended with the highest version number
      //----------------------------------------------------------------------
      directory_stream = opendir(xw_root);
      
      if ( ! directory_stream )
        throw new BackendException("XWHandler::XWHandler  can NOT read "
                                   "folder '%s'", xw_root);
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
          LOG(LOG_DEBUG, "%s(%s)   XW client install folder:      '%s' "
                         "found in '%s'", function_name, instance_name,
                         directory_entry->d_name, xw_root);
          
          file_name     = directory_entry->d_name;
          file_name_str = string(file_name);
          if ( file_name_str.substr(0, xw_client_prefix_length) ==
               xw_client_prefix_str )
          {
            LOG(LOG_DEBUG, "%s(%s)   XW client install folder:      '%s' "
                           "begins with '%s'", function_name, instance_name,
                           file_name, xw_client_prefix_str.c_str());
            
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
            LOG(LOG_INFO, "%s(%s)   XW client install folder:      '%s' is "
                          "candidate with %d.%d.%d",
                          function_name, instance_name, file_name, i, j, k);
          }
        }
        closedir(directory_stream);
        
        if ( xw_file_name_str == "" )
          throw new BackendException("XWHandler::XWHandler  can NOT find "
                                     "'%s'", xw_client_prefix_str.c_str());
        
        g_xw_client_bin_folder_str = xw_root_str + "/" + xw_file_name_str +
                                     "/bin/";
      }
    }
  }
  
  //--------------------------------------------------------------------------
  //  Verify that 'xw_client_bin_folder' is a readable folder.
  //--------------------------------------------------------------------------
  const char * xw_client_bin_folder = g_xw_client_bin_folder_str.c_str();
  LOG(LOG_NOTICE, "%s(%s)  XW client binaries folder:     '%s'",
                  function_name, instance_name, xw_client_bin_folder);
  
  directory_stream = opendir(xw_client_bin_folder);
  if ( directory_stream )
    closedir(directory_stream);
  else
    throw new BackendException("XWHandler::XWHandler  can NOT read folder "
                               "'%s'", xw_client_bin_folder);
  
  //--------------------------------------------------------------------------
  //  Retrieve and log the version number of XtremWeb-HEP
  //--------------------------------------------------------------------------
  auto_ptr< vector<string> > arg_str_vector(new vector<string>);
  arg_str_vector->reserve(1);
  arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwversion");
  returned_t returned_values = outputAndErrorFromCommand(arg_str_vector);
  
  if ( returned_values.retcode == 0 )
  {
    LOG(LOG_NOTICE, "%s(%s)  XW version number:             '%s'",
                    function_name, instance_name,
                    (returned_values.message).c_str());
  }
  else
    throw new BackendException("XWHandler::XWHandler  can NOT get "
                               "XtremWeb-HEP version:  return_code = x'%X'",
                               returned_values.retcode);
  
  //--------------------------------------------------------------------------
  //  Test file name = this.name + pid + TEST
  //--------------------------------------------------------------------------
  char         pid[33];
  string       test_file_path_str;
  const char * test_file_path;
  FILE *       test_file;
  sprintf(pid, "%d", getpid());
  const string test_file_name_str = "/" + name + "_" + pid + "_test.tmp";
  
  //==========================================================================
  //  xw_files_dir
  //==========================================================================
  char * xw_files_folder = g_key_file_get_string(config, instance,
                                                 "xw_files_dir", NULL);
  if ( ! xw_files_folder )
    throw new BackendException("XWHandler::XWHandler  No XtremWeb-HEP files "
                               "folder specified for '%s'", instance);
  
  g_strstrip(xw_files_folder);
  LOG(LOG_NOTICE, "%s(%s)  Folder for XtremWeb-HEP files: '%s'",
                  function_name, instance_name, xw_files_folder);
  
  g_xw_files_folder_str = string(xw_files_folder);
  test_file_path_str    = g_xw_files_folder_str + test_file_name_str;
  test_file_path        = test_file_path_str.c_str();
  LOG(LOG_DEBUG, "%s(%s)   Path for write access test:  '%s'",
                 function_name, instance_name, test_file_path);
  
  test_file = fopen(test_file_path, "w");
  if ( test_file )
    fclose(test_file);
  else
    throw new BackendException("XWHandler::XWHandler  can NOT write to "
                               "folder '%s'", xw_files_folder);
  unlink(test_file_path);
  
  //==========================================================================
  //  [wssubmitter]output-dir
  //==========================================================================
  const char * bridge_output_folder = g_key_file_get_string(config,
                                           "wssubmitter", "output-dir", NULL);
  if ( ! bridge_output_folder )
    throw new BackendException("XWHandler::XWHandler  No bridge output "
                               "files folder specified for '%s'", instance);
  
  LOG(LOG_NOTICE, "%s(%s)  3G Bridge output folder:       '%s'",
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
  
  //==========================================================================
  //  Requested language for localized messages
  //==========================================================================
  const char * var_name     = "LANG";
  const char * current_lang = getenv(var_name);
  LOG(LOG_NOTICE, "%s(%s)  Current   language:            '%s'",
                  function_name, instance_name,
                  (current_lang ? current_lang : ""));
  
  char * requested_lang = g_key_file_get_string(config, instance, var_name,
                                                NULL);
  LOG(LOG_NOTICE, "%s(%s)  Requested language:            '%s'",
                  function_name, instance_name,
                  (requested_lang ? requested_lang : ""));
  
  if ( (requested_lang != NULL)    &&
       ( (current_lang == NULL) ||
         (strcmp(requested_lang, current_lang) != 0) ) )
    g_requested_lang = requested_lang;
  else
    g_requested_lang = NULL;
  LOG(LOG_NOTICE, "%s(%s)  Modifying language:            '%s'",
                  function_name, instance_name,
                  (g_requested_lang ? "yes" : "no"));
    
  //==========================================================================
  //  Forced 'xwclean' before 'xwstatus'
  //==========================================================================
  const char * xwclean_forced = g_key_file_get_string(config, instance,
                                                      "xwclean_forced", NULL);
  g_xwclean_forced = (xwclean_forced != 0) &&
                     (strcasecmp(xwclean_forced, "true") == 0);
  LOG(LOG_NOTICE, "%s(%s)  'xwclean' before 'xwstatus':   '%s'",
                  function_name, instance_name,
                  (g_xwclean_forced ? "yes" : "no"));
  
  //==========================================================================
  //  xw_sleep_time_before_download
  //==========================================================================
  const char * sleep_time_before_download = g_key_file_get_string(config,
                             instance, "xw_sleep_time_before_download", NULL);
  if ( sleep_time_before_download )
    g_sleep_time_before_download = atoi(sleep_time_before_download);
  else
    g_sleep_time_before_download = 0;
  LOG(LOG_NOTICE, "%s(%s)  Sleeping time before download: '%d'",
                  function_name, instance_name, g_sleep_time_before_download);
  
  //==========================================================================
  //  g_xw_apps_message_str
  //==========================================================================
  g_xw_apps_message_str = "";

  //==========================================================================
  //  Ensure LogMon configuration
  //==========================================================================
  logmon::LogMon::instance(config);
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
  LOG(LOG_ERR, "%s(%s)  Job '%s'  Set status of bridge job to 'ERROR'",
               function_name, instance_name, bridge_job_id);
  job->setGridData(message);
  job->setStatus(Job::ERROR);
}


//============================================================================
//
//  Member function  submitJobs
//
//  @param jobs  Vector of pointers to bridge jobs to submit
//
//  This function submits a vector of bridge jobs to XtremWeb-HEP.
//
//============================================================================
void XWHandler::submitJobs(JobVector & jobs) throw (BackendException *)
{
  const char * function_name = "XWHandler::submitJobs";
  const char * instance_name = name.c_str();
  
  LOG(LOG_NOTICE, "%s(%s)  Number of job(s) to be submitted to XtremWeb-HEP "
                  ":  %zd", function_name, instance_name, jobs.size());
  
  if ( jobs.size() < 1 )
      return;
  
  size_t        nb_jobs = 0;
  Job *         job;
  string        bridge_job_id_str;
  const char *  bridge_job_id;
  string        bridge_job_name_str;
  string        submitter_dn_str;
  returned_t    returned_values;
  string        xw_message_str;
  const char *  returned_message;
  auto_ptr< vector<string> > xw_label_vector(new vector<string>);
  auto_ptr< vector<string> > arg_str_vector(new vector<string>);
  auto_ptr< vector<string> > xw_data_uid_str_vector(new vector<string>);
  auto_ptr< vector<string> > xw_env_str_vector(new vector<string>);
  auto_ptr< vector<string> > input_file_str_vector;
  const char *  uid_header      = "UID='";
  const char *  name_header     = "NAME='";
  const size_t  uid_header_len  = strlen(uid_header);
  const size_t  name_header_len = strlen(name_header);
  size_t        pos_quote;
  size_t        pos_appli;
  string        xw_app_id_str;
  string        xw_app_name_str;
  const char *  xw_app_id;
  size_t        input_file_number;
  size_t        input_file_url_without_md5_or_size_number;
  bool          b_xw_data_error;
  string        input_file_name_str;
  FileRef       input_file_ref;
  string        input_file_path_str;
  const char *  input_file_name;
  const char *  input_file_path;
  string        input_file_md5_str;
  const char *  input_file_md5;
  off_t         input_file_size;
  string        xw_input_file_name_str;
  string        xw_input_file_path_str;
  const char *  xw_input_file_name;
  const char *  xw_input_file_path;
  FILE *        xw_input_file;
  string        xw_cmd_xml_file_path_str;
  const char *  xw_cmd_xml_file_path;
  FILE *        xw_cmd_xml_file;
  char          xw_command_xml[BUFSIZ];
  string        xw_data_id_str;
  string        job_batch_id_str;
  string        xw_group_id_str;
  string        xw_sg_id_str;
  string        xw_command_xml_str;
  size_t        pos_xw_id;
  string        xw_job_id_str;
  const char *  xw_job_id;
  
  //--------------------------------------------------------------------------
  //  XtremWeb-HEP label for xwsubmit
  //--------------------------------------------------------------------------
  xw_label_vector->clear();
  xw_label_vector->reserve(2);
  xw_label_vector->push_back(string("--xwlabel"));
  xw_label_vector->push_back("");
  
  
  //==========================================================================
  //
  //  Loop on jobs to be submitted
  //
  //==========================================================================
  for ( JobVector::iterator job_iterator = jobs.begin();
        job_iterator != jobs.end();
        job_iterator++ )
  {
    //========================================================================
    //  Retrieve bridge job id and application
    //  Retrieve submitter DN of the bridge job.  It MUST be present.
    //========================================================================
    job                 = *job_iterator;
    bridge_job_id_str   = job->getId();
    bridge_job_id       = bridge_job_id_str.c_str();
    bridge_job_name_str = job->getName();
    submitter_dn_str    = job->getEnv("PROXY_USERDN");
    if ( submitter_dn_str == "" )
    {
      LOG(LOG_ERR, "%s(%s)  Job '%s'  Environment variable 'PROXY_USERDN' "
                   "missing", function_name, instance_name, bridge_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          "Environment variable 'PROXY_USERDN' missing");
      continue;
    }
    LOG(LOG_NOTICE, "%s(%s)  Job '%s'  Submitter DN = '%s'", function_name,
                    instance_name, bridge_job_id, submitter_dn_str.c_str());
    
    xw_sg_id_str = bridge_job_id_str + ":" + submitter_dn_str;
    
    //========================================================================
    //  Get XtremWeb-HEP application id
    //========================================================================
    xw_app_id = (const char *) NULL;
    
    for ( char num_try = 1; num_try <= 2; num_try++ )
    {
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  Application = '%s'  num_try = %d",
                     function_name, instance_name, bridge_job_id,
                     bridge_job_name_str.c_str(), num_try);
      //----------------------------------------------------------------------
      //  From the list of applications displayed by XtremWeb-HEP, extract the
      //  application id
      //----------------------------------------------------------------------
      pos_appli = 0;
      pos_quote = 0;
      
      while ( (pos_appli != string::npos) && (pos_quote != string::npos) )
      {
        pos_appli = g_xw_apps_message_str.find(uid_header, pos_quote);
        if ( pos_appli != string::npos )
        {
          pos_appli += uid_header_len;
          pos_quote  = g_xw_apps_message_str.find("'", pos_appli);
          if ( pos_quote != string::npos )
          {
            xw_app_id_str = g_xw_apps_message_str.substr(pos_appli,
                                                       pos_quote - pos_appli);
            pos_appli = g_xw_apps_message_str.find(name_header, pos_quote);
            if ( pos_appli != string::npos )
            {
              pos_appli += name_header_len;
              pos_quote  = g_xw_apps_message_str.find("'", pos_appli);
              if ( pos_quote != string::npos )
              {
                xw_app_name_str = g_xw_apps_message_str.substr(pos_appli,
                                                       pos_quote - pos_appli);
                if ( xw_app_name_str == bridge_job_name_str )
                {
                  xw_app_id = xw_app_id_str.c_str();
                  break;
                }
              }
            }
          }
        }
      }
      
      //----------------------------------------------------------------------
      //  If the application has been found in the application list, or if
      //  the application list has already been refreshed, quit the loop.
      //----------------------------------------------------------------------
      if ( (xw_app_id != NULL) || (num_try > 1) )
        break;
      
      //----------------------------------------------------------------------
      //  Here the application has not been found in the application list.
      //  Query applications from XtremWeb-HEP to refresh the application list
      //----------------------------------------------------------------------
      arg_str_vector->clear();
      arg_str_vector->reserve(1);
      arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwapps");
      logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                             arg_str_vector);
      
      returned_values  = outputAndErrorFromCommand(arg_str_vector);
      xw_message_str   = returned_values.message;
      returned_message = xw_message_str.c_str();
      
      LOG(LOG_DEBUG,"%s(%s)  XtremWeb-HEP applications queried.  "
                    "XtremWeb-HEP displayed message = '%s'",
                    function_name, instance_name, returned_message);
      
      if ( returned_values.retcode != 0 )
      {
        LOG(LOG_ERR, "%s(%s)  return_code = x'%X' --> %d  for query of "
                     "XtremWeb-HEP applications.  XtremWeb-HEP displayed "
                     "message = '%s'", function_name, instance_name,
                     returned_values.retcode, returned_values.retcode / 256,
                     returned_message);
        break;
      }
      
      g_xw_apps_message_str = xw_message_str;
    }
    
    if ( xw_app_id == NULL )
    {
      if ( returned_values.retcode == 0 )
      {
        xw_message_str = string("XtremWeb-HEP application '") +
                         bridge_job_name_str + string("' NOT found "
                         "inside message displayed by XtremWeb-HEP");
        LOG(LOG_ERR, "%s(%s)  Job '%s'  %s", function_name, instance_name,
                     bridge_job_id, xw_message_str.c_str());
      }
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          xw_message_str);
      continue;
    }
    
    //========================================================================
    //  Process list of input files
    //========================================================================
    input_file_str_vector = job->getInputs();
    input_file_number     = input_file_str_vector->size();
    
    LOG(LOG_NOTICE, "%s(%s)  Job '%s'  Application = '%s' (%s)  Number of "
                    "input files = %zd", function_name, instance_name,
                    bridge_job_id, bridge_job_name_str.c_str(),
                    xw_app_id, input_file_number);
    
    //------------------------------------------------------------------------
    //  Loop on input files, searching for URLs without MD5 or size.
    //  If any such input file is found, then throw an exception permitting
    //  the 3G Bridge framework to download them.
    //------------------------------------------------------------------------
    input_file_url_without_md5_or_size_number = 0;
    DLException * dl_exception = new DLException(bridge_job_id_str);
    
    for ( vector<string>::iterator input_file_iterator =
                                   input_file_str_vector->begin();
          input_file_iterator !=   input_file_str_vector->end();
          input_file_iterator++ )
    {
      input_file_name_str = *input_file_iterator;
      input_file_ref      = job->getInputRef(input_file_name_str);
      input_file_path_str = input_file_ref.getURL();
      input_file_path     = input_file_path_str.c_str();
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  input_file_name = '%s'  "
                     "input_file_path = '%s'", function_name, instance_name,
                     bridge_job_id, input_file_name_str.c_str(), 
                     input_file_path);
      
      //----------------------------------------------------------------------
      //  Any input file NOT beginning with '/' is an URL.  Get MD5 and size.
      //  If at least one of them is missing, add the URL to the DLException.
      //----------------------------------------------------------------------
      if ( input_file_path_str[0] != '/' )
      {
        input_file_md5_str = input_file_ref.getMD5();
        input_file_size    = input_file_ref.getSize();
        input_file_md5     = input_file_md5_str.c_str();
        if ( (input_file_md5_str == "") || (input_file_size < 0) )
        {
          LOG(LOG_INFO, "%s(%s)  Job '%s'  input_file_name = '%s'  "
                        "input_file_url = '%s'  MD5 = '%s'  size = %jd"
                        "  Missing MD5 or size NOT supported",
                        function_name, instance_name, bridge_job_id,
                        input_file_name_str.c_str(), input_file_path,
                        input_file_md5, (intmax_t)input_file_size);
          input_file_url_without_md5_or_size_number++;
          dl_exception->addInput(input_file_name_str);
        }
      }
    }
    
    if ( input_file_url_without_md5_or_size_number > 0 )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s'  %zd input URLs do NOT have MD5 or "
                      "size.  XtremWeb-HEP does NOT support that.  Therefore,"
                      " the 3G Bridge framework has to download those files.",
                      function_name, instance_name, bridge_job_id,
                      input_file_url_without_md5_or_size_number);
      throw dl_exception;
    }
    
    //------------------------------------------------------------------------
    //  Here all input files should either be local, or they should be an URL
    //  with MD5 and size.  Loop to store each one as an XtremWeb-HEP data.
    //------------------------------------------------------------------------
    xw_data_uid_str_vector->clear();
    b_xw_data_error = false;
    
    for ( vector<string>::iterator input_file_iterator =
                                   input_file_str_vector->begin();
          input_file_iterator !=   input_file_str_vector->end();
          input_file_iterator++ )
    {
      input_file_name_str = *input_file_iterator;
      input_file_name     = input_file_name_str.c_str();
      input_file_ref      = job->getInputRef(input_file_name_str);
      input_file_path_str = input_file_ref.getURL();
      input_file_path     = input_file_path_str.c_str();
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  input_file_name = '%s'  "
                     "input_file_path = '%s'", function_name, instance_name,
                     bridge_job_id, input_file_name, input_file_path);
      
      xw_cmd_xml_file_path = NULL;
      arg_str_vector->clear();
      
      //----------------------------------------------------------------------
      //  Any input file beginning with '/' is local.
      //  Prepare the command to send the data as local file to XtremWeb-HEP.
      //----------------------------------------------------------------------
      if ( input_file_path_str[0] == '/' )
      {
        arg_str_vector->reserve(2);
        arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwsenddata");
        arg_str_vector->push_back(input_file_path_str);
      }
      
      //----------------------------------------------------------------------
      //  Any input file NOT beginning with '/' is an URL
      //----------------------------------------------------------------------
      else
      {
        //--------------------------------------------------------------------
        //  Get MD5 and size
        //--------------------------------------------------------------------
        input_file_md5_str = input_file_ref.getMD5();
        input_file_size    = input_file_ref.getSize();
        input_file_md5     = input_file_md5_str.c_str();
        
        //--------------------------------------------------------------------
        //  Create the XML file describing XtremWeb-HEP data for URL
        //--------------------------------------------------------------------
        xw_cmd_xml_file_path_str = g_xw_files_folder_str +  "/" +
                                   bridge_job_id + "_" + input_file_name_str +
                                   ".xml";
        xw_cmd_xml_file_path     = xw_cmd_xml_file_path_str.c_str();
        LOG(LOG_DEBUG, "%s(%s)  Job '%s'  input_file_name = '%s'  "
                       "input_file_url = '%s'  MD5 = '%s'  size = %jd  "
                       "xw_cmd_xml_file_path = '%s'",
                       function_name, instance_name, bridge_job_id,
                       input_file_name, input_file_path, input_file_md5,
                       (intmax_t)input_file_size, xw_cmd_xml_file_path);
        
        xw_cmd_xml_file = fopen(xw_cmd_xml_file_path, "w");
        if ( ! xw_cmd_xml_file )
        {
          sprintf(xw_command_xml, "input_file_name = '%s'  input_file_url = "
                                  "'%s'  xw_cmd_xml_file_path = '%s'  "
                                  "I/O error = %d  '%s'", input_file_name,
                                  input_file_path, xw_cmd_xml_file_path,
                                  errno, sys_errlist[errno]);
          LOG(LOG_ERR, "%s(%s)  Job '%s'  %s", function_name, instance_name,
                                               bridge_job_id, xw_command_xml);
          xw_message_str  = string(xw_command_xml);
          b_xw_data_error = true;
          break;
        }
        
        sprintf(xw_command_xml,
                "<data name=\"%s\" md5=\"%s\" size=\"%jd\" uri=\"%s\"/>",
                input_file_name, input_file_md5, (intmax_t)input_file_size,
                input_file_path);
      
        LOG(LOG_DEBUG, "%s(%s)  Job '%s'  DATA xw_command_xml = '%s'",
                       function_name, instance_name, bridge_job_id,
                       xw_command_xml);
        fprintf(xw_cmd_xml_file, "%s\n", xw_command_xml);
        fclose(xw_cmd_xml_file);
        
        //--------------------------------------------------------------------
        //  Prepare the command to send the data as URL to XtremWeb-HEP
        //--------------------------------------------------------------------
        arg_str_vector->reserve(3);
        arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwsenddata");
        arg_str_vector->push_back(string("--xwxml"));
        arg_str_vector->push_back(xw_cmd_xml_file_path_str);
      }
      
      //--------------------------------------------------------------------
      //  Send the data to XtremWeb-HEP
      //--------------------------------------------------------------------
      logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                             arg_str_vector);
      
      returned_values  = outputAndErrorFromCommand(arg_str_vector);
      xw_message_str   = returned_values.message;
      returned_message = xw_message_str.c_str();
      LOG(LOG_DEBUG,"%s(%s)  Job '%s'  Data '%s' sent.  XtremWeb-HEP "
                    "displayed message = '%s'", function_name,
                    instance_name, bridge_job_id, input_file_name,
                    returned_message);
      
      if ( returned_values.retcode != 0 )
      {
        LOG(LOG_ERR, "%s(%s)  Job '%s'  Data '%s'  return_code = x'%X' "
                     "--> %d  for XtremWeb-HEP data creation",
                     function_name, instance_name, bridge_job_id,
                     input_file_name, returned_values.retcode,
                     returned_values.retcode / 256);
        b_xw_data_error = true;
        break;
      }
      
      if ( xw_cmd_xml_file_path != NULL )
        unlink(xw_cmd_xml_file_path);
      
      //----------------------------------------------------------------------
      //  From the message displayed by XtremWeb-HEP, extract the DATA id
      //----------------------------------------------------------------------
      pos_xw_id = xw_message_str.rfind("xw://");
      if ( pos_xw_id == string::npos )
      {
        xw_message_str = "'xw://' NOT found inside message displayed by "
                         "XtremWeb-HEP";
        LOG(LOG_ERR, "%s(%s)  Job '%s'  %s", function_name, instance_name,
                     bridge_job_id, xw_message_str.c_str());
        b_xw_data_error = true;
        break;
      }
      
      xw_data_id_str = xw_message_str.substr(pos_xw_id,
                                         xw_message_str.length() - pos_xw_id);
      trimEol(xw_data_id_str);
      xw_data_uid_str_vector->push_back(xw_data_id_str);
    }
    
    if ( b_xw_data_error )
    {
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          xw_message_str);
      continue;
    }
    
    //------------------------------------------------------------------------
    //  If there is exactly 1 input file, then send directly its UID
    //------------------------------------------------------------------------
    if ( input_file_number == 1 )
    {
      xw_env_str_vector->push_back(string("--xwenv"));
      xw_env_str_vector->push_back(xw_data_id_str);
    }
    
    //------------------------------------------------------------------------
    //  If there is more than 1 input file, then send the UID of each one
    //  as data to XtremWeb-HEP using the URIPASSTHROUGH text file.
    //------------------------------------------------------------------------
    else if ( input_file_number > 1 )
    {
      xw_input_file_name_str = string(bridge_job_id) +
                               string("_URIPASSTHROUGH.txt");
      xw_input_file_path_str = g_xw_files_folder_str + "/" +
                               xw_input_file_name_str;
      xw_input_file_name     = xw_input_file_name_str.c_str();
      xw_input_file_path     = xw_input_file_path_str.c_str();
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  URIPASSTHROUGH file path = '%s'",
                     function_name, instance_name, bridge_job_id,
                     xw_input_file_path);
        
      //----------------------------------------------------------------------
      //  -  Open URIPASSTHROUGH text file for writing.
      //  -  For each input file, write its XtremWeb-HEP Data UID inside the
      //     URIPASSTHROUGH text file.
      //  -  Close URIPASSTHROUGH text file.
      //----------------------------------------------------------------------
      xw_input_file = fopen(xw_input_file_path, "w");
      if ( ! xw_input_file )
      {
        sprintf(xw_command_xml, "xw_input_file_path = '%s'  I/O error = "
                                "%d  '%s'", xw_input_file_path, errno,
                                sys_errlist[errno]);
        LOG(LOG_ERR, "%s(%s)  Job '%s'  %s", function_name, instance_name,
                                             bridge_job_id, xw_command_xml);
        xw_message_str  = string(xw_command_xml);
        b_xw_data_error = true;
        setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                            xw_message_str);
        continue;
      }
      
      for ( vector<string>::iterator xw_data_uid_iterator =
                                     xw_data_uid_str_vector->begin();
            xw_data_uid_iterator !=  xw_data_uid_str_vector->end();
            xw_data_uid_iterator++ )
      {
        fprintf(xw_input_file, "%s\n", (*xw_data_uid_iterator).c_str());
      }
      
      fclose(xw_input_file);
      
      //----------------------------------------------------------------------
      //  Send the URIPASSTHROUGH text file as data to XtremWeb-HEP
      //----------------------------------------------------------------------
      arg_str_vector->clear();
      arg_str_vector->reserve(3);
      arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwsenddata");
      arg_str_vector->push_back(string("URIPASSTHROUGH"));
      arg_str_vector->push_back(xw_input_file_path_str);
    
      logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                             arg_str_vector);
      
      returned_values  = outputAndErrorFromCommand(arg_str_vector);
      xw_message_str   = returned_values.message;
      returned_message = xw_message_str.c_str();
      LOG(LOG_DEBUG,"%s(%s)  Job '%s'  Data '%s' sent.  XtremWeb-HEP "
                    "displayed message = '%s'", function_name,
                    instance_name, bridge_job_id, xw_input_file_name,
                    returned_message);
      
      if ( returned_values.retcode != 0 )
      {
        LOG(LOG_ERR, "%s(%s)  Job '%s'  Data '%s'  return_code = x'%X' "
                     "--> %d  for XtremWeb-HEP data creation",
                     function_name, instance_name, bridge_job_id,
                     xw_input_file_name, returned_values.retcode,
                     returned_values.retcode / 256);
        setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                            xw_message_str);
        continue;
      }
      
      //----------------------------------------------------------------------
      //  From the message displayed by XtremWeb-HEP, extract the
      //  URIPASSTHROUGH DATA id
      //----------------------------------------------------------------------
      pos_xw_id = xw_message_str.rfind("xw://");
      if ( pos_xw_id == string::npos )
      {
        xw_message_str = "'xw://' NOT found inside message displayed by "
                         "XtremWeb-HEP";
        LOG(LOG_ERR, "%s(%s)  Job '%s'  %s", function_name, instance_name,
                     bridge_job_id, xw_message_str.c_str());
        setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                            xw_message_str);
        continue;
      }
      
      xw_data_id_str = xw_message_str.substr(pos_xw_id,
                                         xw_message_str.length() - pos_xw_id);
      trimEol(xw_data_id_str);
      xw_env_str_vector->push_back(string("--xwenv"));
      xw_env_str_vector->push_back(xw_data_id_str);
    }
    
    //========================================================================
    //  If '_3G_BRIDGE_BATCH_ID' exists among the environment variables of the
    //  bridge job, it should be an already existing XtremWeb-HEP 'group_id'
    //========================================================================
    job_batch_id_str = job->getEnv("_3G_BRIDGE_BATCH_ID");
    if ( job_batch_id_str.length() > 0 )
    {
      LOG(LOG_DEBUG, "%s(%s)  Job '%s'  Job batch id = '%s'", function_name,
                     instance_name, bridge_job_id, job_batch_id_str.c_str());
      xw_group_id_str = job_batch_id_str;
      xw_env_str_vector->push_back(string("--xwgroup"));
      xw_env_str_vector->push_back(xw_group_id_str);
    }
    
    //========================================================================
    //  Create the XML file describing XtremWeb-HEP job
    //========================================================================
    xw_cmd_xml_file_path_str = g_xw_files_folder_str +  "/" + bridge_job_id +
                               ".xml";
    xw_cmd_xml_file_path     = xw_cmd_xml_file_path_str.c_str();
    LOG(LOG_DEBUG, "%s(%s)  Job '%s'  xw_cmd_xml_file_path = '%s'",
                   function_name, instance_name, bridge_job_id,
                   xw_cmd_xml_file_path);
    
    xw_cmd_xml_file = fopen(xw_cmd_xml_file_path, "w");
    if ( ! xw_cmd_xml_file )
    {
      LOG(LOG_ERR, "%s(%s)  Job '%s'  xw_cmd_xml_file_path = '%s'  "
                   "I/O error = %d  '%s'",
                   function_name, instance_name, bridge_job_id,
                   xw_cmd_xml_file_path, errno, sys_errlist[errno]);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          sys_errlist[errno]);
      continue;
    }
    
    //------------------------------------------------------------------------
    //  As long as XtremWeb-HEP accepts at most one input file,
    //  'xw_data_id_str' has at most 1 value.
    //------------------------------------------------------------------------
    xw_command_xml_str = string("<work appuid=\"") + xw_app_id_str +
                         string("\" cmdline=\"")   + job->getArgs()  + "\"";
    if ( input_file_number > 0 )
      xw_command_xml_str += string(" dirinuri=\"")  + xw_data_id_str  + "\"";
    if ( job_batch_id_str.length() > 0 )
      xw_command_xml_str += string(" groupUID=\"") + xw_group_id_str + "\"";
    xw_command_xml_str   += string(" sgid=\"")     + xw_sg_id_str    + "\"/>";
    LOG(LOG_DEBUG, "%s(%s)  Job '%s'  JOB xw_command_xml = '%s'",
                   function_name, instance_name, bridge_job_id,
                   xw_command_xml_str.c_str());
    fprintf(xw_cmd_xml_file, "%s\n", xw_command_xml_str.c_str());
    fclose(xw_cmd_xml_file);
        
    //========================================================================
    //  Submit the job to XtremWeb-HEP
    //========================================================================
    arg_str_vector->clear();
    arg_str_vector->reserve(3);
    arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwsubmit");
    arg_str_vector->push_back(string("--xwxml"));
    arg_str_vector->push_back(xw_cmd_xml_file_path_str);
    
//  const string specials_str  = " !$&()*/?[\\]^{|}~";
//  size_t submitter_dn_length = submitter_dn_str.length();
//  size_t pos_special         = submitter_dn_str.find_first_of(specials_str);
//  while ( pos_special < submitter_dn_length )
//  {
//    submitter_dn_str[pos_special] = (submitter_dn_str[pos_special] == '/') ?
//                                      ',' :
//                                      '_';
//    pos_special++;
//    if ( pos_special < submitter_dn_length )
//      pos_special = submitter_dn_str.find_first_of(specials_str,
//                                                   pos_special);
//  }
//  xw_label_vector->back() = (submitter_dn_str == "") ?
//                            bridge_job_id_str:
//                            bridge_job_id_str + ":" + submitter_dn_str;
//  
//  arg_str_vector->reserve(3 + xw_label_vector->size() +
//                          xw_env_str_vector->size());
//  arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwsubmit");
//  arg_str_vector->insert(arg_str_vector->end(),
//                         xw_label_vector->begin(),
//                         xw_label_vector->end());
//  arg_str_vector->push_back(job->getName());
//  arg_str_vector->push_back(job->getArgs());
//  arg_str_vector->insert(arg_str_vector->end(), xw_env_str_vector->begin(),
//                                                xw_env_str_vector->end());
    
    logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                           arg_str_vector);
    
    returned_values  = outputAndErrorFromCommand(arg_str_vector);
    returned_message = (returned_values.message).c_str();
    
    LOG(LOG_DEBUG,"%s(%s)  Job '%s' submitted.  XtremWeb-HEP displayed "
                  "message = '%s'", function_name, instance_name,
                  bridge_job_id, returned_message);
    
    if ( returned_values.retcode != 0 )
    {
      LOG(LOG_ERR, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                   "XtremWeb-HEP submission.  XtremWeb-HEP displayed message "
                   "= '%s'", function_name, instance_name, bridge_job_id,
                   returned_values.retcode, returned_values.retcode / 256,
                   returned_message);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          returned_values.message);
      continue;
    }
        
    unlink(xw_cmd_xml_file_path);
    
    //========================================================================
    //  From the message displayed by XtremWeb-HEP, extract the XtremWeb-HEP
    //  job id
    //========================================================================
    pos_xw_id = (returned_values.message).rfind("xw://");
    if ( pos_xw_id == string::npos )
    {
      LOG(LOG_ERR, "%s(%s)  Job '%s'  'xw://' NOT found inside message "
                   "displayed by XtremWeb-HEP",
                   function_name, instance_name, bridge_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          returned_values.message);
      continue;
    }
    
    xw_job_id_str = (returned_values.message).substr(pos_xw_id,
                              (returned_values.message).length() - pos_xw_id);
    trimEol(xw_job_id_str);
    xw_job_id = xw_job_id_str.c_str();
    
    //========================================================================
    // Insert the XtremWeb-HEP job id in the database
    // Set the bridge status of the job to RUNNING
    //========================================================================
    LOG(LOG_NOTICE, "%s(%s)  Job '%s'  Set status of bridge job to 'RUNNING'",
                    function_name, instance_name, bridge_job_id);
    job->setGridId(xw_job_id_str);
    job->setStatus(Job::RUNNING);
    
    LOG(LOG_NOTICE, "%s(%s)  Job '%s'  XtremWeb-HEP Job ID = '%s'",
                    function_name, instance_name, bridge_job_id, xw_job_id);

    logmon::LogMon::instance().createMessage()
	    .add("event", "job_submission")
	    .add("job_id", bridge_job_id)
	    .add("job_id_dg", xw_job_id)
	    .add("output_grid_name", instance_name)
	    .save();
    logmon::LogMon::instance().createMessage()
	    .add("event", "job_status")
	    .add("job_id", bridge_job_id)
	    .add("status", "Running")
	    .save();
    
    nb_jobs++;
  }
  LOG(LOG_NOTICE, "%s(%s)  Number of job(s) successfully submitted to "
                  "XtremWeb-HEP :  %zd / %zd",
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
//  @param job             Pointer to the bridge job to update
//  @param xw_job_status   XtremWeb-HEP status of the job
//  @param xw_message_str  XtremWeb-HEP message associated to the job
//
//  This function updates the status of a given bridge job :
//  - From the XtremWeb-HEP status of the job, calculate the new bridge status
//  - Store the new bridge status and the XtremWeb-HEP message in the database
//
//============================================================================
void XWHandler::updateJobStatus(Job *          job,
                                const string & xw_job_current_status,
                                const string & xw_message_str)
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
  
  LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  XtremWeb-HEP job status = '%s'  "
                 "Bridge job old status = %d has perhaps to be updated",
                 function_name, instance_name, bridge_job_id, xw_job_id,
                 xw_job_status, bridge_job_old_status);
    
  if ( xw_message_str != job->getGridData() )
  {
    LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  XtremWeb-HEP message = '%s'",
                    function_name, instance_name, bridge_job_id, xw_job_id,
                    xw_message_str.c_str());
    job->setGridData(xw_message_str);
  }
  
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
        //--------------------------------------------------------------------
        //  Try to extract the completion date of the XtremWeb-HEP job.
        //  Initial value of the 'xw_date_str' variable is the empty string.
        //--------------------------------------------------------------------
        string xw_date_str = "";
        
        if ( (xw_job_current_status == "COMPLETED") ||
             (xw_job_current_status == "ERROR")     ||
             (xw_job_current_status == "ABORTED")   )
        {
          const char * date_header = "COMPLETEDDATE='";
          size_t pos_date          = xw_message_str.find(date_header);
          if ( pos_date != string::npos )
          {
            pos_date += strlen(date_header);
            size_t pos_quote = xw_message_str.find("'", pos_date);
            if ( pos_quote != string::npos )
              xw_date_str = xw_message_str.substr(pos_date,
                                                  pos_quote - pos_date);
          }
        }
        LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  XtremWeb-HEP job status = "
                        "'%s'  Bridge job new status = %d  Completed date = "
                        "'%s'", function_name, instance_name, bridge_job_id,
                        xw_job_id, xw_job_status, bridge_job_new_status,
                        xw_date_str.c_str());
        job->setStatus(bridge_job_new_status);
      
        LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  XtremWeb-HEP job status = '%s'"
                       "  Bridge job new status = %d successfully stored",
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
//    Destroy the corresponding XtremWeb-HEP job.
//  - RUNNING :
//    Query XtremWeb-HEP for the status of the corresponding XtremWeb-HEP job.
//    If the XtremWeb-HEP job is COMPLETED, retrieve the XtremWeb-HEP results.
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
  //  If the bridge status of the job is CANCEL, delete it from XtremWeb-HEP
  //--------------------------------------------------------------------------
  if ( bridge_status == Job::CANCEL )
  {
    LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  Bridge job status = 'CANCEL'",
                    function_name, instance_name, bridge_job_id, xw_job_id);
    
    if ( xw_job_id_str.empty() )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  has NO XtremWeb-HEP id",
                      function_name, instance_name, bridge_job_id, xw_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          string("Cancelled  (NO XtremWeb-HEP id)"));
      return;
    }
    
    arg_str_vector->clear();
    arg_str_vector->reserve(2);
    arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwrm");
    arg_str_vector->push_back(xw_job_id_str);
    logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                           arg_str_vector);
    
    returned_values = outputAndErrorFromCommand(arg_str_vector);
    
    //------------------------------------------------------------------------
    //  Update the bridge job status only if cancellation is successful or
    //  if XtremWeb-HEP error message contains 'not enough rights to delete'
    //------------------------------------------------------------------------
    string xw_message_str = returned_values.message;
    
    if ( returned_values.retcode == 0 )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  successfully removed from "
                      "XtremWeb-HEP",
                      function_name, instance_name, bridge_job_id, xw_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          string("Cancelled.  ") + returned_values.message);
    }
    else if ( ( xw_message_str.find("not found") != string::npos ) ||
              ( xw_message_str.find("not enough rights to delete") !=
                string::npos ) )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  NOT found by XtremWeb-HEP",
                      function_name, instance_name, bridge_job_id, xw_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          string("Cancelled  (NOT found by XtremWeb-HEP)"));
    }
    else
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  XtremWeb-HEP return code = "
                      "x'%X' --> %d  -->  3G Bridge status left unchanged",
                      function_name, instance_name, bridge_job_id, xw_job_id,
                      returned_values.retcode, returned_values.retcode / 256);
    }
    
    return;
  }
  
  //--------------------------------------------------------------------------
  //  If the bridge status of the job is RUNNING, refresh it from XtremWeb-HEP
  //--------------------------------------------------------------------------
  if ( bridge_status == Job::RUNNING )
  {
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  Bridge job status = 'RUNNING'",
                  function_name, instance_name, bridge_job_id, xw_job_id);
    
    //------------------------------------------------------------------------
    //  Force XtremWeb-HEP to refresh its cache.
    //  This should NOT be necessary anymore.
    //------------------------------------------------------------------------
    if ( g_xwclean_forced )
    {
      arg_str_vector->clear();
      arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwclean");
      logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                             arg_str_vector);
      returned_values = outputAndErrorFromCommand(arg_str_vector);
    }
    
    //------------------------------------------------------------------------
    //  Query job status from XtremWeb-HEP
    //------------------------------------------------------------------------
    arg_str_vector->clear();
    arg_str_vector->reserve(2);
    arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwstatus");
    arg_str_vector->push_back(xw_job_id_str);
    logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                           arg_str_vector);
    
    returned_values = outputAndErrorFromCommand(arg_str_vector);
    
    //------------------------------------------------------------------------
    //  Try to parse the message displayed by XtremWeb-HEP
    //------------------------------------------------------------------------
    string xw_message_str = returned_values.message;
    
    if ( xw_message_str.find("ERROR : object not found") != string::npos )
    {
      LOG(LOG_ERR, "%s(%s)  Job '%s' (%s)  NOT found by XtremWeb-HEP",
                   function_name, instance_name, bridge_job_id, xw_job_id);
      setJobStatusToError(function_name, instance_name, bridge_job_id, job,
                          returned_values.message);
      return;
    }
    
    if ( returned_values.retcode != 0 )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  XtremWeb-HEP return code = "
                      "x'%X' --> %d  -->  3G Bridge status left unchanged",
                      function_name, instance_name, bridge_job_id, xw_job_id,
                      returned_values.retcode, returned_values.retcode / 256);
      return;
    }
    
    string xw_job_status_str = "";
    string xw_job_label_str  = "";
    
    //------------------------------------------------------------------------
    //  Message displayed by XtremWeb-HEP should contain:  STATUS='<status>'
    //------------------------------------------------------------------------
    const char * status_header = "STATUS='";
    size_t       pos_status    = xw_message_str.find(status_header);
    size_t       pos_quote     = 0;
    if ( pos_status != string::npos )
    {
      pos_status += strlen(status_header);
      pos_quote   = xw_message_str.find("'", pos_status);
      if ( pos_quote != string::npos )
        xw_job_status_str = xw_message_str.substr(pos_status,
                                                  pos_quote - pos_status );
    }
    
    //------------------------------------------------------------------------
    //  Message displayed by XtremWeb-HEP should contain:  LABEL='<label>'
    //------------------------------------------------------------------------
    const char * label_header = "LABEL='";
    size_t       pos_label    = xw_message_str.find(label_header);
    pos_quote = 0;
    if ( pos_label != string::npos )
    {
      pos_label += strlen(label_header);
      pos_quote  = xw_message_str.find("'", pos_label);
      if ( pos_quote != string::npos )
        xw_job_label_str = xw_message_str.substr(pos_label,
                                                 pos_quote - pos_label );
    }
    
    //------------------------------------------------------------------------
    //  If XtremWeb-HEP job status is NOT found, leave 3G Bridge status
    //  unchanged
    //------------------------------------------------------------------------
    if ( xw_job_status_str == "" )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  XtremWeb-HEP status NOT found "
                      "--> 3G Bridge status left unchanged",
                      function_name, instance_name, bridge_job_id,
                      xw_job_id);
      return;
    }
    
    //------------------------------------------------------------------------
    //  XtremWeb-HEP status of the job permits to update bridge job status
    //------------------------------------------------------------------------
    LOG(LOG_INFO, "%s(%s)  Job '%s' (%s)  XtremWeb-HEP job status = '%s'",
                  function_name, instance_name, bridge_job_id, xw_job_id,
                  xw_job_status_str.c_str());
    string xw_status_message_str =
                 xw_message_str.substr(xw_message_str.find_first_not_of(' '));
    
    //------------------------------------------------------------------------
    //  If the XtremWeb-HEP status of the job is COMPLETED :
    //  - Retrieve its results from XtremWeb-HEP,
    //  - Extract the output files required by the bridge.
    //------------------------------------------------------------------------
    if ( xw_job_status_str == "COMPLETED" )
    {
      LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  XtremWeb-HEP job status = '%s'",
                      function_name, instance_name, bridge_job_id, xw_job_id,
                      xw_job_status_str.c_str());
      
      //----------------------------------------------------------------------
      //  If the current folder is not the folder for XtremWeb-HEP output
      //  files, go there.
      //----------------------------------------------------------------------
      char * cwd = getcwd((char*)NULL, 0);
      LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  cwd = '%s'", function_name, 
                     instance_name, bridge_job_id, xw_job_id, cwd);
      
      const char * workdir = g_xw_files_folder_str.c_str();
      if  ( strcmp(cwd, workdir) != 0 )
      {
        LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  Executing chdir('%s')",
                       function_name, instance_name, bridge_job_id, xw_job_id,
                       workdir);
        chdir(workdir);
      }
      
      free(cwd);             // 'cwd' has been dynamically allocated by getcwd
      
      //----------------------------------------------------------------------
      //  Retrieve the results of the XtremWeb-HEP job
      //----------------------------------------------------------------------
      arg_str_vector->clear();
      arg_str_vector->reserve(4);
      arg_str_vector->push_back(g_xw_client_bin_folder_str + "xwresults");
      arg_str_vector->push_back(xw_job_id_str);
      arg_str_vector->push_back("--xwformat");
      arg_str_vector->push_back("xml");
      logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                             arg_str_vector);
      
      returned_values = outputAndErrorFromCommand(arg_str_vector);
      
      if ( returned_values.retcode != 0 )
      {
        LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  XtremWeb-HEP return code = "
                        "x'%X' --> %d  -->  3G Bridge status left unchanged",
                        function_name, instance_name, bridge_job_id,
                        xw_job_id, returned_values.retcode,
                        returned_values.retcode / 256);
      }
      else
      {
        xw_message_str = returned_values.message;
        LOG(LOG_NOTICE, "%s(%s)  Job '%s' (%s)  Results successfully "
                        "retrieved from XtremWeb-HEP  '%s'",
                        function_name, instance_name, bridge_job_id,
                        xw_job_id, xw_message_str.c_str());
      
        //  Sleep some time before download.  Should NOT be necessary.
        if ( g_sleep_time_before_download > 0 )
        {
          LOG(LOG_DEBUG, "%s(%s)  Job '%s'  Sleeping time before download = "
                         "%d", function_name, instance_name, bridge_job_id,
                         g_sleep_time_before_download);
          sleep(g_sleep_time_before_download);
        }
    
        //--------------------------------------------------------------------
        //  Message displayed by XtremWeb-HEP should contain:  resulturi="
        //  Name of the result file begins with result URI after last '/'
        //--------------------------------------------------------------------
        string       xw_result_uid_str = "*";        // By default, a star
        const char * result_header     = "resulturi=\"";
        size_t       pos_result        = xw_message_str.find(result_header);
        size_t       pos_last_slash;
        pos_quote = 0;
        if ( pos_result != string::npos )
        {
          pos_result += strlen(result_header);
          pos_quote   = xw_message_str.find('"', pos_result);
          if ( pos_quote != string::npos )
          {
            xw_result_uid_str = xw_message_str.substr(pos_result,
                                                      pos_quote - pos_result);
            pos_last_slash = xw_result_uid_str.rfind('/');
            LOG(LOG_DEBUG, "%s(%s)  xw_result_uri = '%s'  "
                           "pos_last_slash=%zd",
                           function_name, instance_name,
                           xw_result_uid_str.c_str(), pos_last_slash);
            
            if ( pos_last_slash != string::npos )
              xw_result_uid_str =
                             xw_result_uid_str.substr(pos_last_slash + 1);
          }
        }
        
        //--------------------------------------------------------------------
        //  XtremWeb-HEP job UID is XtremWeb-HEP job URI after last '/'
        //--------------------------------------------------------------------
        string xw_job_uid_str = xw_job_id_str;
        pos_last_slash        = xw_job_id_str.rfind('/');
        LOG(LOG_DEBUG, "%s(%s)  xw_job_id = '%s'  pos_last_slash=%zd",
                       function_name, instance_name, xw_job_id,
                       pos_last_slash);
        
        if ( pos_last_slash != string::npos )
          xw_job_uid_str = xw_job_id_str.substr(pos_last_slash + 1);
        
        //--------------------------------------------------------------------
        //  Name of ZIP file :  ATTENTION :
        //  If the XtremWeb-HEP label is not empty, it ends with it,
        //  otherwise it ends with the XtremWeb-HEP job UID.
        //--------------------------------------------------------------------
        if ( xw_job_label_str == "" )
          xw_job_label_str = xw_job_uid_str;
        
        string xw_zip_file_name_str = xw_result_uid_str + "_ResultsOf_" +
                                      xw_job_label_str + ".zip";
        string xw_zip_file_path_str = g_xw_files_folder_str +
                                      "/ResultsOf_" + xw_job_uid_str + ".zip";
        
        //--------------------------------------------------------------------
        //  The XtremWeb-HEP ZIP file containing the job results should be in
        //  the XtremWeb-HEP file folder.
        //  Rename this ZIP file with a shorter name.
        //--------------------------------------------------------------------
        const char * command = "/bin/mv";
        int    return_code;
        
        if ( xw_result_uid_str == "*" )
        {
          arg_str_vector->clear();
          arg_str_vector->reserve(3);
          arg_str_vector->push_back(string("/bin/sh"));
          arg_str_vector->push_back(string("-c"));
          arg_str_vector->push_back(string(command)       + "  " +
                                    g_xw_files_folder_str + "/"  +
                                    xw_zip_file_name_str  + "  " +
                                    xw_zip_file_path_str);
          logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                                 arg_str_vector);
          
          returned_values  = outputAndErrorFromCommand(arg_str_vector);
          returned_message = (returned_values.message).c_str();
          return_code      = returned_values.retcode;
          LOG(LOG_DEBUG, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                         "command '%s'  Displayed message = '%s'",
                         function_name, instance_name, bridge_job_id,
                         return_code, return_code / 256, command,
                         returned_message);
        }
        else
        {
          xw_zip_file_name_str = g_xw_files_folder_str + "/" +
                                 xw_zip_file_name_str;
          LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  rename('%s', '%s')",
                         function_name, instance_name, bridge_job_id,
                         xw_job_id, xw_zip_file_name_str.c_str(),
                         xw_zip_file_path_str.c_str());
          
          return_code = rename(xw_zip_file_name_str.c_str(),
                               xw_zip_file_path_str.c_str());
            
          LOG(LOG_DEBUG, "%s(%s)  Job '%s' (%s)  rename('%s', '%s')  "
                         "return_code = %d  errno = %d  '%s'",
                         function_name, instance_name, bridge_job_id,
                         xw_job_id, xw_zip_file_name_str.c_str(),
                         xw_zip_file_path_str.c_str(), return_code, errno,
                         sys_errlist[errno]);
          
          if (return_code == 0)
          {
            returned_message = "";
          }
          else
          {
            returned_message = sys_errlist[errno];
            return_code      = 256 * errno;
          }
        }
        
        if ( return_code == 0 )
        {
          //------------------------------------------------------------------
          //  Get the list of bridge ouptut files to be extracted from the
          //  XtremWeb-HEP ZIP file to the bridge output files folder.
          //  We suppose that all output files have to go to the same folder,
          //  otherwise we would have to generate 1 unzip command per output
          //  file.
          //------------------------------------------------------------------
          command = "/usr/bin/unzip";
          auto_ptr< vector<string> > output_file_names = job->getOutputs();
          arg_str_vector->clear();
          arg_str_vector->reserve(5 + output_file_names->size());
          arg_str_vector->push_back(string(command));
          arg_str_vector->push_back(string("-o"));
          arg_str_vector->push_back(xw_zip_file_path_str);
          arg_str_vector->insert(arg_str_vector->end(),
                                 output_file_names->begin(),
                                 output_file_names->end());
          arg_str_vector->push_back(string("-d"));
          string output_file_name = output_file_names->back();
          string output_file_path = job->getOutputPath(output_file_name);
          size_t pos_last_slash   = output_file_path.rfind(string("/") +
                                                           output_file_name);
          arg_str_vector->push_back(output_file_path.substr(0,
                                                            pos_last_slash));
          
          logStringVectorToDebug(function_name, instance_name, bridge_job_id,
                                 arg_str_vector);
          
          //------------------------------------------------------------------
          //  Extract the bridge output files from the ZIP file containing
          //  XtremWeb-HEP results, then delete it.
          //------------------------------------------------------------------
          returned_values  = outputAndErrorFromCommand(arg_str_vector);
          returned_message = (returned_values.message).c_str();
          return_code      = returned_values.retcode;
          LOG(LOG_DEBUG, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                         "command '%s'  Displayed message = '%s'",
                         function_name, instance_name, bridge_job_id,
                         return_code, return_code / 256, command,
                         returned_message);
          
        }
        
        if ( return_code == 0 )
        {
          unlink(xw_zip_file_path_str.c_str());
        }
        else
        {
          LOG(LOG_ERR, "%s(%s)  Job '%s'  return_code = x'%X' --> %d  for "
                       "command '%s'  Displayed message = '%s'",
                       function_name, instance_name, bridge_job_id,
                       return_code, return_code / 256, command,
                       returned_message);
          setJobStatusToError(function_name, instance_name, bridge_job_id,
                              job, returned_message);
          return;
        }

	logmon::LogMon::instance().createMessage()
		.add("event", "job_status")
		.add("job_id", bridge_job_id)
		.add("status", "Finished")
		.save();
      }
    }
    
    //------------------------------------------------------------------------
    //  For the bridge job, store :
    //  -  the bridge status        inside the 'status'  attribute,
    //  -  the XtremWeb-HEP message inside the 'griddata' attribute.
    //------------------------------------------------------------------------
    updateJobStatus(job, xw_job_status_str, xw_status_message_str);
  }
}


//============================================================================
//  Factory function
//============================================================================
HANDLER_FACTORY(config, instance)
{
  return new XWHandler(config, instance);
}
