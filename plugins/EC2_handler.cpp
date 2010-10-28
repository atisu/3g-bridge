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

#include "EC2Util.h"
#include "Util.h"
#include "DBHandler.h"
#include "EC2_handler.h"

#include <vector>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <map>
#include <vector>
#include <sstream>
#include <typeinfo>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>


using namespace std;


EC2Handler::EC2Handler(GKeyFile *config, const char *instance): GridHandler(config, instance)
{
    cf_user_data = NULL;
    cf_region = NULL;
    environment_data = NULL;
    
    parseConfig(config, instance);
}


void EC2Handler::submitJob(Job *job) throw (BackendException *)
{
   logit(LOG_DEBUG, "%s::%s() called.", typeid(*this).name(), __FUNCTION__);

   gchar *my_args;
   gchar** my_argv;
   int my_argc;
   gchar* image_id = NULL;
   gchar* kernel_id = NULL;
   gchar* ramdisk_id = NULL;
   gchar* instance_type = NULL;
   gchar* group = NULL;
   GError *error = NULL;
   GOptionContext *context;
   GOptionEntry entries[] = 
   {
       {"image", 'i', 0, G_OPTION_ARG_STRING, &image_id, "Machine Image", "AMI"},
       {"kernel", 'k', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_STRING, &kernel_id, "Kernel Image", "AKI"},       
       {"ramdisk", 'r', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_STRING, &ramdisk_id, "Ramdisk Image", "ARI"},
       {"instance-type", 't', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_STRING, &instance_type, "Instance Type", "TYPE"},
       {"group", 'g', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_STRING, &group, "Security Group", "GROUP"},      
       {NULL}
   };
   string job_id;

   /* Parse command line arguments
    *
    * Format should look like: image=<IMAGE_ID>,kernel=<KERNEL_ID>,ramdisk=<RAMDISK_ID>,size=<INSTANCE_SIZE>,group=<SECURITY_GROUP>
    * 'image' is required, everything else is optional.
    * name is ignored but must be the first element in an argv array
    * (Documentation states: A side-effect of calling g_option_context_parse
    * is that g_set_prgname() will be called. We may not want this...)
    */

   my_args = g_strjoin(" ", job->getName().c_str(), job->getArgs().c_str(), NULL);
   my_argv = g_strsplit(my_args, " ", -1);
   g_free(my_args);
   my_argc = g_strv_length(my_argv);

   context = g_option_context_new("- Amazon EC2/ Eucalyptus plug-in for 3G-Bridge");
   g_option_context_add_main_entries(context, entries, NULL);
   if (!g_option_context_parse(context, &my_argc, &my_argv, &error))
   {
       job->setStatus(Job::ERROR);
       logit(LOG_DEBUG, "%s::%s(): cannot parse job arguments: '%s'", 
           typeid(*this).name(), __FUNCTION__, job->getArgs().c_str());
       return;
   }
   g_strfreev(my_argv);
   g_option_context_free(context);
   
   GString* my_arguments = g_string_new(NULL);   
   if (!image_id)
   {
        /* image_id is mandatory */
       job->setStatus(Job::ERROR);
       logit(LOG_DEBUG, "%s::%s(): AMI is not provided with argument '--image=<AMI>'", 
           typeid(*this).name(), __FUNCTION__);       
       g_string_free(my_arguments, TRUE);
       g_free(image_id);
       g_free(kernel_id);
       g_free(ramdisk_id);
       g_free(instance_type);
       g_free(group);
       return;
   }
   my_arguments = g_string_append(my_arguments, image_id);
   g_free(image_id);       
   if (kernel_id)
   {
       g_string_append_printf(my_arguments, " --kernel %s", kernel_id);
       g_free(kernel_id);
   }
   if (ramdisk_id)
   {
       g_string_append_printf(my_arguments, " --ramdisk %s", ramdisk_id);       
       g_free(ramdisk_id);
   }
   if (instance_type)
   {
       g_string_append_printf(my_arguments, " --instance-type %s", instance_type);       
       g_free(instance_type);       
   }
   if (group)
   {
       g_string_append_printf(my_arguments, " --group %s", group);
       g_free(group);
   }

   /* region is from config file */
   g_string_append_printf(my_arguments, " --region %s", cf_region);
   
   try 
   {
       job_id = createVMInstance(my_arguments->str);
   } 
   catch (BackendException *e) 
   {
       // job failed now, try later
       job->setStatus(Job::TEMPFAILED);
       logit(LOG_DEBUG, "%s::%s(): Cannot create new instance: %s", 
           typeid(*this).name(), __FUNCTION__, e->what());
       delete e;
       g_string_free(my_arguments, TRUE);
       return;
   }
   g_string_free(my_arguments, TRUE);
   job->setGridId(job_id);
   job->setStatus(Job::RUNNING);
}


void EC2Handler::cancelJob(Job *job)
{
    try 
    {
        terminateVMInstance(job->getGridId());
    } 
    catch (BackendException *e) 
    {
        logit(LOG_DEBUG, "%s::%s(): Cannot terminate instance: %s", 
            typeid(*this).name(), __FUNCTION__, e->what());
        delete e;
        return;        
    }
    logit(LOG_DEBUG, "%s::%s(): Job '%s' (VM with instance id %s) cancelled.", 
        typeid(*this).name(), __FUNCTION__, job->getId().c_str(), 
        job->getGridId().c_str());
    DBHandler *dbh = DBHandler::get();
    dbh->deleteJob(job->getId());
    DBHandler::put(dbh);	
}


void EC2Handler::submitJobs(JobVector &jobs) throw (BackendException *) 
{
    for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
    {
        Job *actualJob = *it;
        submitJob(actualJob);
    }
}


EC2Handler::~EC2Handler()
{	
    // if pointer is NULL it simply returns.
    g_free(cf_region);
    g_free(cf_user_data);

    logit(LOG_DEBUG, "EC2Handler: destructor");
}


void EC2Handler::updateStatus(void) throw (BackendException *)
{
    /*
     *  Get status from EC2
     */
    int return_value = 0;
    int tries = 1;
    int max_tries = 5;
    string output_text;
   
    gchar* commandline = g_strdup_printf("ec2-describe-instances --show-empty-fields --region %s", cf_region);
    gchar** myargs = g_strsplit(commandline, " ", -1);
    g_free(commandline);

    do 
    {
        return_value = invoke_cmd(myargs, environment_data, &output_text);
        if (return_value != 0) 
        {
 	        if (tries > max_tries) 
 	        {
 	            logit(LOG_DEBUG,"%s::%s(): queriing instances failed: %s",
 	                typeid(*this).name(), __FUNCTION__, output_text.c_str());
                    g_strfreev(myargs);		     
                    return;
 	        }        
 	        logit(LOG_DEBUG, "%s::%s(): queriing instances failed, retrying (%d of %d)", 
 	            typeid(*this).name(), __FUNCTION__, tries, max_tries);
            tries++; 
         } 
         else 
         {
             last_updatejob_reply = output_text;
         }
     } 
     while (return_value != 0);


    /*
     *  update jobs
     */
   DBHandler *jobDB = DBHandler::get();
   jobDB->pollJobs(this, Job::RUNNING, Job::CANCEL);
   DBHandler::put(jobDB);
   g_strfreev(myargs);
}


void EC2Handler::poll(Job *job) throw (BackendException *)
{
	switch (job->getStatus())
	{
		case Job::RUNNING:
			updateJob(job);
			break;
		case Job::CANCEL:
			cancelJob(job);
			break;
		default:
            break;
	}
}


void EC2Handler::updateJob(Job *job)
{
    int found = 0;
    string output_line;
    
    stringstream output_stream(last_updatejob_reply);
    while (getline(output_stream, output_line, '\n'))
    {
        if (output_line.compare(0, 8, "INSTANCE") != 0)
 	        continue;
        stringstream output_line_stream(output_line);
        string output_token;
        vector<string> tokens;
        while (getline(output_line_stream, output_token, '\t'))
 	        tokens.push_back(output_token);

        string instance_id = tokens[1];
        string instance_status = tokens[5];
         
        if (instance_id != job->getGridId())
            continue;
        found = 1;
        if (instance_status == "terminated" && job->getStatus() == Job::RUNNING) 
        {
            logit(LOG_DEBUG, "%s::%s(): Instance '%s' was terminated from outside.", 
                typeid(*this).name(), __FUNCTION__, instance_id.c_str());
            job->setStatus(Job::ERROR);             
        }
    }
    if (found == 0)
    {
        logit(LOG_DEBUG, "%s::%s(): Instance '%s' was terminated from outside.", 
                typeid(*this).name(), __FUNCTION__, job->getGridId().c_str());
        job->setStatus(Job::ERROR);                     
    }
}


void EC2Handler::cleanJob(Job *job)
{

}


GridHandler *EC2Handler::getInstance(GKeyFile *config, const char *instance)
{
    return new EC2Handler(config, instance);
}


string EC2Handler::createVMInstance(gchar* args) throw (BackendException *)
{
    string output_text;
    string instance_id;

    gchar* commandline = g_strdup_printf("ec2-run-instances %s %s", args, cf_user_data);
    gchar** myargs = g_strsplit(commandline, " ", -1);
    g_free(commandline);
 
    logit(LOG_DEBUG, "%s::%s(): starting an instance..", typeid(*this).name(), __FUNCTION__);

    if (invoke_cmd(myargs, environment_data, &output_text) != 0) 
    {
        g_strfreev(myargs);
        throw new BackendException("%s::%s(): running instances failed: (ec2-run-instances %s %s) %s",
		    typeid(*this).name(), __FUNCTION__, 
		    args, cf_user_data,
		    output_text.c_str());
    }
    g_strfreev(myargs);
   
    string reply_line;
    stringstream reply_stream(output_text);
    while (getline(reply_stream, reply_line, '\n')) 
    {  
        if (reply_line.compare(0, 8, "INSTANCE") != 0)
            continue;
        stringstream reply_line_stream(reply_line);
        string reply_token;
        vector<string> tokens;
        while (getline(reply_line_stream, reply_token, '\t'))
    	       tokens.push_back(reply_token);
        logit(LOG_DEBUG, "%s::%s(): %s/%s => %s - '%s'", 
            typeid(*this).name(), __FUNCTION__,
            tokens[2].c_str(), tokens[1].c_str(), tokens[3].c_str(), tokens[5].c_str());
        instance_id = tokens[1];
        break;
    }
    return instance_id;
}


void EC2Handler::terminateVMInstance(string instance_id) throw (BackendException *)
{
   int result;
   string command_result;
   gchar* commandline = g_strdup_printf("ec2-terminate-instances --region %s %s", cf_region, instance_id.c_str());
   gchar** myargs = g_strsplit(commandline, " ", -1);
   g_free(commandline);
   
   result = invoke_cmd(myargs, environment_data, &command_result);
   g_strfreev(myargs);
   if (result != 0) {
      throw new BackendException("%s::%s(): could not terminate instance '%s'", 
        typeid(*this).name(), __FUNCTION__, instance_id.c_str());
   }  
}


void EC2Handler::parseConfig(GKeyFile *config, const char *instance) throw (BackendException *)
{
    GString* environment = g_string_new(NULL);
    gchar*   config_value;

    /*
     * region (required) - non-environment variable
     */
    cf_region = g_key_file_get_string(config, instance, "region", NULL);
    if (cf_region == NULL) {
        g_string_free(environment, TRUE);
        throw new BackendException("%s::%s(): 'region' is not set for %s", 
            typeid(*this).name(), __FUNCTION__, instance);
    }
    g_strstrip(cf_region);
    logit(LOG_DEBUG, "%s::%s(): 'region' is '%s'", typeid(*this).name(), __FUNCTION__, cf_region);

    /* 
     * java-home (required) 
     */
    config_value = g_key_file_get_string(config, instance, "java-home", NULL);
    if (config_value == NULL) {
        g_string_free(environment, TRUE);
        throw new BackendException("%s::%s(): 'java-home' is not set for %s", 
            typeid(*this).name(), __FUNCTION__, instance);        
    }
    g_strstrip(config_value);
    g_string_append_printf(environment, "JAVA_HOME=%s", config_value);
    logit(LOG_DEBUG, "%s::%s(): 'java-home' is '%s'", typeid(*this).name(), __FUNCTION__, config_value);
    g_free(config_value);

    /* 
     * ec2-home (required) 
     */
    config_value = g_key_file_get_string(config, instance, "ec2-home", NULL);
    if (config_value == NULL) {
        g_string_free(environment, TRUE);
        throw new BackendException("%s::%s(): 'ec2-home' is not set for %s", 
            typeid(*this).name(), __FUNCTION__, instance);        
    }
    g_strstrip(config_value);
    g_string_append_printf(environment, " EC2_HOME=%s", config_value);
    logit(LOG_DEBUG, "%s::%s(): 'ec2-home' is '%s'", typeid(*this).name(), __FUNCTION__, config_value);
    /* add ec2-home to path */
    gchar *path = getenv("PATH");
    g_string_append_printf(environment, " PATH=%s/bin:%s", config_value, path);
    g_free(config_value);
    
    /*
     * ec2-private-key (required)
     */
    config_value = g_key_file_get_string(config, instance, "ec2-private-key", NULL);    
    if (config_value == NULL) {
        g_string_free(environment, TRUE);
        throw new BackendException("%s::%s(): 'ec2-private-key' is not set for %s", 
            typeid(*this).name(), __FUNCTION__, instance);
    }
    g_strstrip(config_value);
    g_string_append_printf(environment, " EC2_PRIVATE_KEY=%s", config_value);
    logit(LOG_DEBUG, "%s::%s(): 'ec2-private-key' is '%s'", typeid(*this).name(), __FUNCTION__, config_value);
    g_free(config_value);

    /*
     * ec2-certificate (required)
     */
    config_value = g_key_file_get_string(config, instance, "ec2-certificate", NULL);
    if (config_value == NULL) {
        g_string_free(environment, TRUE);
        throw new BackendException("%s::%s(): 'ec2-certificate' is not set for %s", 
            typeid(*this).name(), __FUNCTION__, instance);
    }
    g_strstrip(config_value);
    g_string_append_printf(environment, " EC2_CERT=%s", config_value);
    logit(LOG_DEBUG, "%s::%s(): 'ec2-certificate' is '%s'", typeid(*this).name(), __FUNCTION__, config_value);
    g_free(config_value);

    /*
     * ec2-service-url (not required for Amazon EC2 - required for Eucalyptus)
     */
    config_value = g_key_file_get_string(config, instance, "ec2-service-url", NULL);
    if (config_value != NULL) {
        g_strstrip(config_value);
        g_string_append_printf(environment, " EC2_URL=%s", config_value);
        logit(LOG_DEBUG, "%s::%s(): 'ec2-service-url' is '%s'", typeid(*this).name(), __FUNCTION__, config_value);
        g_free(config_value);
    } else
        logit(LOG_DEBUG, "%s::%s(): 'ec2-service-url' is not set for %s", 
            typeid(*this).name(), __FUNCTION__, instance);

    /*
     * user-data (optional)
     */
    gchar* t_user_data = g_key_file_get_string(config, instance, "user-data", NULL);
    /*
     * user-data-file (optional)
     */
    gchar* t_user_data_file = g_key_file_get_string(config, instance, "user-data-file", NULL);
    if (t_user_data_file != NULL) {
        g_strstrip(t_user_data_file);
        cf_user_data = g_strdup_printf(" -f %s", t_user_data_file);
        g_free(t_user_data_file);
    } else if (t_user_data != NULL) {
        g_strstrip(t_user_data);
        cf_user_data = g_strdup_printf(" -d %s", t_user_data);
        g_free(t_user_data);
    } else {
        logit(LOG_WARNING, "%s::%s(): both 'user-data' and 'user-data-file' are empty", 
		    typeid(*this).name(), __FUNCTION__);
        cf_user_data = g_strdup_printf(" ");
    }
    logit(LOG_DEBUG, "%s::%s(): user specified data is (with '-f' / '-d' prefix) '%s'", 
        typeid(*this).name(), __FUNCTION__, cf_user_data);

    name = instance;
    groupByNames = false;

    g_string_append_printf(environment, " %d", NULL);
    g_strfreev(environment_data);
    environment_data = g_strsplit(environment->str, " ", -1);
    g_string_free(environment, TRUE);
}


/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new EC2Handler(config, instance);
}

