#ifndef __EGEEHANDLER_H
#define __EGEEHANDLER_H

#include "CGJob.h"
#include "DBHandler.h"
#include "GridHandler.h"

#include <string>
#include <vector>
#include <glite/wms/wmproxyapi/wmproxy_api.h>
#include <globus_ftp_client.h>


using namespace std;
using namespace glite::wms;
using namespace glite::wms::wmproxyapi;


class EGEEHandler : public GridHandler {
    public:
	EGEEHandler(DBHandler *jDB, const string &WMProxy_EndPoint) throw (BackendException &);
	~EGEEHandler();
	void submitJobs(vector<CGJob *> *jobs) throw (BackendException &);
	void updateStatus(void) throw (BackendException &);
	void cancelJobs(vector<CGJob *> *jobs) throw (BackendException &);
	void getStatus(vector<CGJob *> *jobs) throw (BackendException &);

	/* Obsolete */
	void getOutputs(vector<CGJob *> *jobs) throw (BackendException &);
    private:
	static const int GSIFTP_BSIZE = 1024000;
	static const int SUCCESS = 0;
	static globus_mutex_t lock;
	static globus_cond_t cond;
	static globus_bool_t done;
	static bool globus_err;
	static int global_offset;
	ConfigContext *cfg;
	DBHandler *jobDB;
	void init_ftp_client(globus_ftp_client_handle_t *ftp_handle, globus_ftp_client_handleattr_t *ftp_handle_attrs, globus_ftp_client_operationattr_t *ftp_op_attrs);
	void destroy_ftp_client(globus_ftp_client_handle_t *ftp_handle, globus_ftp_client_handleattr_t *ftp_handle_attrs, globus_ftp_client_operationattr_t *ftp_op_attrs);
	static void handle_finish(void *user_args, globus_ftp_client_handle_t *ftp_handle, globus_object_t *error);
	static void handle_data_write(void *user_args, globus_ftp_client_handle_t *handle, globus_object_t *error, globus_byte_t *buffer, globus_size_t buflen, globus_off_t offset, globus_bool_t eof);
	static void handle_data_read(void *user_args, globus_ftp_client_handle_t *ftp_handle, globus_object_t *error, globus_byte_t *buffer, globus_size_t buflen, globus_off_t offset, globus_bool_t eof);
	void upload_file_globus(const vector<string> &inFiles, const string &destURI);
	void download_file_globus(const vector<string> &remFiles, const vector<string> &locFiles);
	void delete_file_globus(const vector<string> &fileNames, const string &prefix = "");
	void cleanJob(const string &jobID);
	void delegate_Proxy(const string& delID);
	void throwStrExc(const char *func, const BaseException &e) throw(BackendException &);
	void throwStrExc(const char *func, const string &str) throw(BackendException &);
	void renew_proxy();
	void getOutputs_real(CGJob *jobs);
};

#endif
