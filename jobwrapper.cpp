/*
 * Copyright (C) 2008 MTA SZTAKI LPDS
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

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <uuid/uuid.h>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <glib.h>
#include <mysql.h>

#include "Util.h"
#include "boinc_api.h"
#include "miofile.h"
#include "parse.h"

// all filepath is relative from the slot dir (e.g. boincClientDir/slots/0 )
#define JOBWRAPPER_CLIENT_XML	"../../jobwrapper_client.xml"
#define JOBWRAPPER_CONFIG_FILE	"jobwrapper.config"
#define IDFILE			"3g_run.id"

#define MAX_LENGTH		10000
#define MAX_FILES		100

using namespace std;

extern void boinc_sleep(double seconds);

static char bridgecfg[PATH_MAX];			// 3G-Bridge config file name
static char cfgsection[128];				// Section to use in bridgecfg
static MYSQL *conn;					// MySQL connection
static char *grid;					// The grid where the WU should be run
static char *mysql_dbname;				// The MySQL database name
static char *mysql_host;				// The MySQL server host
static char *mysql_user;				// The MySQL server user
static char *mysql_pass;				// The MySQL server password

static char *cwd;					// Working directory of the workunit
static char *wuname;					// Name of the workunit
static int app_files_num;				// how many files the application has
static char *app_files[MAX_FILES];			// what is the name of these files the application thinks
static char *app_files_real_name[MAX_FILES];		// what is the REAL name of these files the BOINC store

static int input_files_num;				// the same with input files 
static char *input_files[MAX_FILES];			// e.g. in.txt
static char *input_files_real_name[MAX_FILES];		// e.g. distribute.txt_df4fg4h6h443fnjx3_0

static int output_files_num;				// the same with output files
static char *output_files[MAX_FILES];			// e.g. out.txt
static char *output_files_real_name[MAX_FILES];

static char arguments[MAX_LENGTH];			// the arguments for the exec
static char exec_path[MAX_LENGTH];			// the executable

static char *wrap_template =
"#!/bin/bash\n"
"\n"
"TID=%s\n"
"SLOT=%s\n"
"EXE=\"%s\"\n"
"ARGS=\"%s\"\n"
"OLDWD=\"`pwd`\"\n"
"rm -rf /tmp/$TID; mkdir -p /tmp/$TID\n"
"tar zxf $TID.tgz -C /tmp/$TID\n"
"rm -f $TID.tgz\n"
"cd /tmp/$TID/slots/$SLOT\n"
"\"../../$EXE\" $ARGS\n"
"RETCODE=$?\n"
"cd ../..\n"
"cat slots/$SLOT/$TID.outs | while read outfile; do\n"
"    tar uf $OLDWD/$TID.out.tar \"$outfile\"\n"
"done\n"
"cd \"$OLDWD\"; rm -rf /tmp/$TID\n"
"gzip -9 $TID.out.tar\n"
"mv $TID.out.tar.gz $TID.out.tgz\n"
"exit $RETCODE\n";


extern bool update_app_progress(double cpu_t, double cp_cpu_t);


void init_config(void)
{
	FILE *f = NULL;
	MIOFILE mf;
	char buf[256];
	GKeyFile *kf;
	GError *error = NULL;

	// First get bridgecfg and cfgsection from jobwrapper_client.xml
	if (NULL == (f = fopen(JOBWRAPPER_CLIENT_XML, "r"))) {
		LOG(LOG_ERR, "Cannot open %s/%s", cwd, JOBWRAPPER_CLIENT_XML);
		exit(-1);
	}
	mf.init_file(f);
	while(mf.fgets(buf, sizeof(buf))) {
		if (parse_str(buf, "<bridge_conf>", bridgecfg, sizeof(bridgecfg))) continue;
		if (parse_str(buf, "<conf_section>", cfgsection, sizeof(cfgsection))) continue;
	}
	fclose(f);
	if (bridgecfg[0] == '\0' || cfgsection[0] == '\0') {
		LOG(LOG_ERR, "Could not get <bridge_conf> and <conf_section> from %s/%s", cwd, JOBWRAPPER_CLIENT_XML);
		exit(-1);
	}

	// Now read 3G bridge stuff
	kf = g_key_file_new();
	if (!g_key_file_load_from_file(kf, bridgecfg, G_KEY_FILE_NONE, &error)) {
		LOG(LOG_ERR, "Failed to load the config file %s: %s\n", bridgecfg, error->message);
		g_error_free(error);
		exit(-1);
	}

	log_init(kf, cfgsection);

	mysql_dbname = g_key_file_get_string(kf, "database", "name", &error);
	if (!mysql_dbname) {
		LOG(LOG_ERR, "Failed to get database name: %s\n", error->message);
		g_error_free(error);
		exit(-1);
	}
	mysql_host = g_key_file_get_string(kf, "database", "host", &error);
	if (!mysql_host) {
		LOG(LOG_ERR, "Failed to get database host: %s\n", error->message);
		g_error_free(error);
		exit(-1);
	}
	mysql_user = g_key_file_get_string(kf, "database", "user", &error);
	if (!mysql_user) {
		LOG(LOG_ERR, "Failed to get database user: %s\n", error->message);
		g_error_free(error);
		exit(-1);
	}
	mysql_pass = g_key_file_get_string(kf, "database", "password", &error);
	if (!mysql_pass) {
		LOG(LOG_ERR, "Failed to get database password: %s\n", error->message);
		g_error_free(error);
		exit(-1);
	}
	grid = g_key_file_get_string(kf, cfgsection, "grid", &error);
	if (!grid) {
		LOG(LOG_ERR, "Failed to get %s grid: %s\n", cfgsection, error->message);
		g_error_free(error);
		exit(-1);
	}

	g_key_file_free(kf);
}


void corrupt(void)
{
	char cmd[MAX_LENGTH];

	snprintf(cmd, sizeof(cmd), "cp %s ../../", JOBWRAPPER_CONFIG_FILE);
	if (system(cmd) != 0)
    		LOG(LOG_ERR, "Error:  The %s file is corrupt. Cannot copy the file into the BoincClientDir.", JOBWRAPPER_CONFIG_FILE);
	else
	        LOG(LOG_ERR, "Error:  The %s file is corrupt. Copied into the BoincClientDir.", JOBWRAPPER_CONFIG_FILE);
	exit(-1);
}


void read_config(void)
{
        FILE *jobwrapper_config_file;
        char buf[MAX_LENGTH];
        char *p;
        int len;
        int i = 0;

        jobwrapper_config_file = fopen(JOBWRAPPER_CONFIG_FILE, "r");
        if (jobwrapper_config_file == NULL) {
		LOG(LOG_ERR, "Error, cannot open jobwrapper config file: %s,\n  errno=%d %s",
			JOBWRAPPER_CONFIG_FILE, errno, strerror(errno));
		exit(-108);
        }
        while (fgets(buf, sizeof(buf), jobwrapper_config_file)) {
		if ((strstr(buf, "app_files_num:")) != NULL) {
			p = (char *)strchr(buf, ' ');
			if (p == NULL)
				corrupt();
			p++;
			app_files_num = atoi(p);
			if (app_files_num > MAX_FILES) {
				LOG(LOG_ERR, "Error:  Application has got too many files %d. Maxmimum app file is %d.",
					app_files_num, MAX_FILES);
				exit(-1);
			}
			if (app_files_num < 1) {
				LOG(LOG_ERR, "Error:  Application hasn't got any application files according to the %s.\n %s",
					JOBWRAPPER_CONFIG_FILE, buf);
				exit(-1);
			}
			for (i = 0; i < app_files_num; i++) {
				fgets(buf, sizeof(buf), jobwrapper_config_file);
				if (!strcmp(buf, ""))
					corrupt();
				len = strcspn(buf, " ");
				if (len > MAX_LENGTH - 1)
					len = MAX_LENGTH - 1;
				if (len < 1)
					corrupt();
				app_files_real_name[i] = (char *) malloc (len + 1);
				snprintf(app_files_real_name[i], len + 1, "%s", buf);

				p = (char *)strchr(buf, ' ');
				if (p == NULL)
					corrupt();
				p++;
				len = strcspn(p, "\n");
				if (len > MAX_LENGTH - 1)
					len = MAX_LENGTH - 1;
				if (len < 1)
					corrupt();
				app_files[i] = (char *) malloc(len + 1);
				snprintf(app_files[i], len + 1, "%s", p);
			}
		} else if ((strstr(buf, "input_files_num:")) != NULL) {
		        p = (char *)strchr(buf, ' ');
			if (p == NULL)
				corrupt();
			p++;
			input_files_num = atoi(p);
			if (input_files_num > MAX_FILES) {
				LOG(LOG_ERR, "Error:  Application has got too many input files %d. Maximum input file is %d",
					input_files_num, MAX_FILES);
				exit(-1);
			}
			if (input_files_num < 0){
				LOG(LOG_ERR, "Error:  Application has got too few input files %d.",
					input_files_num);
				exit(-1);
			}
			for (i = 0; i < input_files_num; i++) {
				fgets(buf, sizeof(buf), jobwrapper_config_file);
				if (!strcmp(buf, ""))
					corrupt();
				len = strcspn(buf, " ");
				if (len > MAX_LENGTH - 1)
					len = MAX_LENGTH - 1;
				if (len < 1)
					corrupt();
				input_files_real_name[i] = (char *) malloc (len + 1);
				snprintf(input_files_real_name[i], len + 1, "%s", buf);

				p = (char *)strchr(buf, ' ');
				if (p == NULL)
					corrupt();
				p++;
				len = strcspn(p, "\n");
				if (len > MAX_LENGTH - 1)
					len = MAX_LENGTH - 1;
				if (len < 1)
					corrupt();
				input_files[i] = (char *) malloc(len + 1);
				snprintf(input_files[i], len + 1, "%s", p);
			}
		} else if ((strstr(buf, "output_files_num:")) != NULL) {
			p = (char *)strchr(buf, ' ');
			if (p == NULL)
				corrupt();
			p++;
			output_files_num = atoi(p);
			if (output_files_num > MAX_FILES) {
				LOG(LOG_ERR, "Error:  Application has got too many output files %d. Maximum output files is %d.",
					output_files_num, MAX_FILES);
				exit(-1);
			}
			if (output_files_num < 1) {
				LOG(LOG_ERR, "Error:  Application has got too few output files %d.",
					output_files_num);
				exit(-1);
			}
			for (i = 0; i < output_files_num; i++) {
				fgets(buf, sizeof(buf), jobwrapper_config_file);
				if (!strcmp(buf, ""))
					corrupt();
				if (NULL != strstr(buf, "command_line:")) {
					output_files_num = i;
					goto finread;
				}
				len = strcspn(buf, " ");
				if (len > MAX_LENGTH - 1)
					len = MAX_LENGTH - 1;
				if (len < 1)
					corrupt();
				output_files_real_name[i] = (char *) malloc (len + 1);
				snprintf(output_files_real_name[i], len + 1, "%s", buf);

				p = (char *)strchr(buf, ' ');
				if (p == NULL)
					corrupt();
				p++;
				len = strcspn(p, "\n");
				if (len > MAX_LENGTH - 1)
					len = MAX_LENGTH - 1;
				if (len < 1)
					corrupt();
				output_files[i] = (char *) malloc(len + 1);
				snprintf(output_files[i], len + 1, "%s", p);
			}
		} else if ((strstr(buf, "command_line:")) != NULL) {
finread:
			p = strdup(buf);
			p = strtok(buf, " ");
			p = strtok(NULL, " ");

			len = strcspn(p, " ");
			if (len > MAX_LENGTH - 1)
				len = MAX_LENGTH - 1;
			if (len < 1)
				corrupt();
        		snprintf(exec_path, len + 1, "%s", p);
			p += len+1;/*p = strtok(NULL, " ");*/
			len = strcspn(p, "\n");
			if (len > MAX_LENGTH - 1)
				len = MAX_LENGTH - 1;
			if (len > 0)
            			snprintf(arguments, len + 1, "%s", p);
			else
				sprintf(arguments, " ");
		} else {
			corrupt(); // undefinied line
		}
	}    
	fclose(jobwrapper_config_file);
}


string add_to_3g_db(char *slotStr)
{
	uuid_t uID;
	FILE *wrapF, *outF;
	char sID[37], wrapFname[64], outsFname[64], *wrapStr;

	uuid_generate(uID);
	uuid_unparse(uID, sID);
	string exe_path(exec_path);
	string exe_path_bn = exe_path.substr(exe_path.rfind("/")+1);
	asprintf(&wrapStr, wrap_template, sID, slotStr, exec_path, arguments);
	sprintf(wrapFname, "%s.sh", sID);
        sprintf(outsFname, "%s.outs", sID);
	if (NULL == (wrapF = fopen(wrapFname, "w"))) {
        	LOG(LOG_ERR, "Error: unable to open file \"%s\" for writing: %s", wrapFname, strerror(errno));
	        return "";
	}
	if (NULL == (outF = fopen(outsFname, "w"))) {
        	LOG(LOG_ERR, "Error: unable to open file \"%s\" for writing: %s", outsFname, strerror(errno));
	        return "";
	}
	fwrite(wrapStr, 1, strlen(wrapStr), wrapF);
	for (int i = 0; i < output_files_num; i++)
        	fprintf(outF, "%s\n", output_files_real_name[i]);
	fprintf(outF, "slots\n");
        fclose(wrapF);
	fclose(outF);
        free(wrapStr);

	string infiles = "";
        for (int i = 0; i < input_files_num; i++)
	        infiles += "\"" + string(input_files_real_name[i]) + "\" ";
			for (int i = 0; i < app_files_num; i++)
		        	infiles += "\"" + string(app_files_real_name[i]) + "\" ";

        stringstream tcomm;
	tcomm << "tar zcf " << sID << ".tgz -C ../.. slots/" << slotStr << " " << infiles;
	LOG(LOG_DEBUG, "Creating input tgz for \"%s\" with command \"%s\".", sID, tcomm.str().c_str());
	system(tcomm.str().c_str());

	LOG(LOG_INFO, "Inserting WU \"%s\" into 3G Job Database with identifier \"%s\".", wuname, sID);

	char *query;
	asprintf(&query, "INSERT INTO cg_job(id, alg, grid, status, args) VALUES (\"%s\",\"%s\",\"%s\",\"%s\",\"%s\")",
		sID, wrapFname, grid, "PREPARE", arguments);
	LOG(LOG_DEBUG, "MySQL    job insert command is: %s", query);
	if (mysql_query(conn, query))
	{
		LOG(LOG_ERR, "Error: failed to add job entry to database: %s", mysql_error(conn));
		free(query);
		return "";
	}
	free(query);

	asprintf(&query, "INSERT INTO cg_inputs VALUES(\"%s\",\"%s\",\"%s/%s\")",
		sID, wrapFname, cwd, wrapFname);
	LOG(LOG_DEBUG, "MySQL  inputs insert command is: %s", query);
	if (mysql_query(conn, query))
	{
		LOG(LOG_ERR, "Error: failed to add job entry to database: %s", mysql_error(conn));
		free(query);
		return "";
	}
	free(query);

	asprintf(&query, "INSERT INTO cg_inputs VALUES(\"%s\",\"%s.tgz\",\"%s/%s.tgz\")",
		sID, sID, cwd, sID);
	LOG(LOG_DEBUG, "MySQL outputs insert command is: %s", query);
	if (mysql_query(conn, query))
	{
		LOG(LOG_ERR, "Error: failed to add job entry to database: %s", mysql_error(conn));
		free(query);
		return "";
	}
	free(query);

	asprintf(&query, "INSERT INTO cg_outputs VALUES(\"%s\", \"%s.out.tgz\", \"%s/%s.out.tgz\")",
		sID, sID, cwd, sID);
	if (mysql_query(conn, query))
	{
		LOG(LOG_ERR, "Error: failed to add job entry to database: %s", mysql_error(conn));
		free(query);
		return "";
	}
	free(query);

	asprintf(&query, "UPDATE cg_job SET status=\"INIT\" where id=\"%s\"", sID);
	if (mysql_query(conn, query))
	{
		LOG(LOG_ERR, "Error: failed to add job entry to database: %s", mysql_error(conn));
		free(query);
		return "";
	}
	free(query);

	ofstream idF(IDFILE);
	idF << string(sID) << endl;
	idF.close();

	return string(sID);
}


string getstat_3g(string jobID)
{
	char *query;
	MYSQL_RES *res;
	MYSQL_ROW row;

	LOG(LOG_DEBUG, "About to update status of job \"%s\".", jobID.c_str());
	asprintf(&query, "SELECT status FROM cg_job WHERE id=\"%s\"", jobID.c_str());
	if (mysql_query(conn, query))
	{
		LOG(LOG_ERR, "Error: failed to get status of job \"%s\": %s", jobID.c_str(), mysql_error(conn));
		free(query);
		return "UNKNOWN";
	}
	if (!(res = mysql_store_result(conn)))
	{
		LOG(LOG_ERR, "Error: failed to get status of job \"%s\": %s", jobID.c_str(), mysql_error(conn));
		free(query);
		return "UNKNOWN";
	}
	row = mysql_fetch_row(res);
	if (!row)
	{
		LOG(LOG_ERR, "Error: failed to get status of job \"%s\": %s", jobID.c_str(), mysql_error(conn));
		free(query);
		return "UNKNOWN";
	}
	string rv(row[0]);
	free(query);
	mysql_free_result(res);
	return rv;
}


void clean_3g(string jobID)
{
	char *query;
	asprintf(&query, "DELETE FROM cg_job WHERE id=\"%s\"", jobID.c_str());
	if (mysql_query(conn, query))
	{
		LOG(LOG_ERR, "Error: failed to delete job \"%s\": %s", jobID.c_str(), mysql_error(conn));
	}
	free(query);

	unlink((jobID + ".sh").c_str());
	unlink((jobID + ".tgz").c_str());
	unlink((jobID + ".out.tgz").c_str());
}


void init_mysql()
{
	LOG(LOG_DEBUG, "Trying to establish connection to MySQL DB \"%s\" on host \"%s\" with user \"%s\".", mysql_dbname, mysql_host, mysql_user);
	conn = mysql_init(0);
	if (!conn)
	{
		LOG(LOG_ERR, "Failed to initialize MySQL: %s", mysql_error(conn));
		exit(-1);
	}
	if (!mysql_real_connect(conn, mysql_host, mysql_user, mysql_pass, mysql_dbname, 0, 0, 0))
	{
		LOG(LOG_ERR, "Failed to connect to MySQL server: %s", mysql_error(conn));
		exit(-1);
	}
	LOG(LOG_INFO, "MySQL connection OK.\n");
}

int main(int argc, char **argv)
{
	int exitcode = 0;
	string jobID, status = "";

	cwd = getcwd(NULL, 0);
	if (!cwd) {
		LOG(LOG_ERR, "Failed to determine working directory!");
		exit(-1);
	}
	init_config();
	LOG(LOG_INFO, "Jobwrapper pid=%d starts", getpid());
	LOG(LOG_DEBUG, "CWD=\"%s\"\n", cwd);
	for (int i = 0 ; i < argc; i++)
		LOG(LOG_DEBUG, "argv[%d]=\"%s\"\n", i, argv[i]);

	BOINC_OPTIONS opts;
	boinc_options_defaults(opts);
	opts.send_status_msgs = 0;
	boinc_init_options(&opts);

	char *slotStr, *wdcpy;
	wdcpy = strdup(cwd);
	slotStr = basename(wdcpy);

	// Reading out the needed informations for the job and the 3G bridge
	read_config();

        APP_INIT_DATA aid2;
	FILE *aidf2 = fopen("init_data.xml", "r");
	parse_init_data_file(aidf2, aid2);
	fclose(aidf2);
	wuname = strdup(aid2.wu_name);
	LOG(LOG_INFO, "About to handle workunit \"%s\" through 3G bridge.", wuname);

	// Initialize MySQL connection
	init_mysql();

	ifstream idF(IDFILE);
	if (!idF)
	{
		LOG(LOG_INFO, "Job ID file for WU not present, submitting a new job.");
		jobID = add_to_3g_db(slotStr);
		if (jobID == "")
		{
			LOG(LOG_ERR, "Error: 3G Bridge database identifier is not set!");
			idF.close();
			exit(-1);
		}
	} else {
		idF >> jobID;
		LOG(LOG_INFO, "Job ID file for WU exists, the job ID is \"%s\".", jobID.c_str());
		idF.close();
	}
	LOG(LOG_INFO, "Our 3G Bridge identifier is \"%s\".\n", jobID.c_str());
	free(wdcpy);

	status = getstat_3g(jobID);
	while ("FINISHED" != status && "ERROR" != status) {
		LOG(LOG_DEBUG, "Status of job \"%s\" is \"%s\".", jobID.c_str(), status.c_str());
		boinc_sleep(300);
		status = getstat_3g(jobID);
	}

        LOG(LOG_INFO, "Job \"%s\" finished with status \"%s\".", jobID.c_str(), status.c_str());
        if ("FINISHED" == status) {
		stringstream tcomm;
    		tcomm << "tar zxf " << jobID << ".out.tgz -C ../..";
    		LOG(LOG_DEBUG, "Executing command to unpack output results: \"%s\".", tcomm.str().c_str());
    		system(tcomm.str().c_str());
    		exitcode = 0;
	}

	clean_3g(jobID);
	mysql_close(conn);

	FILE *f;
	if (NULL != (f = fopen("boinc_finish_called", "w")))
    		fclose(f);

	LOG(LOG_INFO, "Job \"%s\" finished with jobid \"%s\". The exitcode was %d.", exec_path, jobID.c_str(), exitcode);

	APP_INIT_DATA aid;
	FILE *aidf = fopen("init_data.xml", "r");
	parse_init_data_file(aidf, aid);
	fclose(aidf);
	for (int i = 0; i < 3; i++) {
    		boinc_report_app_status(aid.wu_cpu_time, 0, 100);
		sleep(1);
	}

	char msg_buf[MSG_CHANNEL_SIZE];
	sprintf(msg_buf,
    		"<current_cpu_time>%10.4f</current_cpu_time>\n"
    		"<checkpoint_cpu_time>%.15e</checkpoint_cpu_time>\n"
    		"<fraction_done>%2.8f</fraction_done>\n",
    		aid.wu_cpu_time, 0.0, 100.0);
	app_client_shm->shm->app_status.send_msg(msg_buf);

	boinc_exit(exitcode);
	return exitcode;
}
