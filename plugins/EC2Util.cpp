/*
 * Copyright (C) 2009-2010 MTA SZTAKI LPDS
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
 * In addition, as a special exception, MTA SZTAKI gives you permission to link the
 * code of its release of the 3G Bridge with the OpenSSL project's "OpenSSL"
 * library (or with modified versions of it that use the same license as the
 * "OpenSSL" library), and distribute the linked executables. You must obey the
 * GNU General Public License in all respects for all of the code used other than
 * "OpenSSL". If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version.
 */
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <cstdlib>
#include "EC2Util.h"


using namespace std;


int invoke_cmd(const char *const argv[], const char *const env[], string *stdoe) throw (BackendException *)
{
    int sock[2];                                                                                               
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock) == -1)                                                       
        throw new BackendException("socketpair() failed: %s", strerror(errno));
    pid_t pid = fork();                                                                                        
    if (pid)                                                                                                   
    {
        /* Parent */                                                                                       
        int status;                                                                                        
        char buf[1024];                                                                                    
        close(sock[0]);                                                                                    
        if (stdoe)                                                                                         
        {
            *stdoe = "";                                                                               
            while ((status = read(sock[1], buf, sizeof(buf))) > 0)                                     
                stdoe->append(buf, status);                                                        
        }
        close(sock[1]);                                                                                    
        if (waitpid(pid, &status, 0) == -1)                                                                
            throw new BackendException("waitpid() failed: %s", strerror(errno));
        if (WIFSIGNALED(status))
            throw new BackendException("Command %s died with signal %d", 
                argv[0], WTERMSIG(status));
        return WEXITSTATUS(status);                                                                        
    }
    /* Child */                                                                                                
    int fd = open("/dev/null", O_RDWR);                                                                        
    dup2(fd, 0);                                                                                               
    close(fd);                                                                                                 

    dup2(sock[0], 1);                                                                                          
    dup2(sock[0], 2);                                                                                          
    close(sock[0]);                                                                                            
    close(sock[1]);                                                                                            

    /* 
     * execve() takes envp[], but does not search for file in path,
     * so stick with execvp() and set environment manually.
     */
    gchar **env_data = (gchar **)env;
    if (**env_data != NULL) {
        while (*env_data != NULL) {
            putenv(*env_data);
            env_data++;
        }
    }
    
    execvp(argv[0], (char *const *)argv);
    perror("execvp");
    _Exit(255);                                                                                                
}
