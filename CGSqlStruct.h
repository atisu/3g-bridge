#ifndef __CGSQLSTRUCT_H
#define __CGSQLSTRUCT_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <mysql++/mysql++.h>

#ifdef HAVE_MYSQL___SSQLS_H
#include <mysql++/ssqls.h>
#else
#include <mysql++/custom.h>
#endif

#include <string>

using namespace std;
using namespace mysqlpp;

sql_create_8(cg_job, 1, 8,
    string,	id,
    string, 	args,
    string,	alg,
    string,	status,
    string,	gridid,
    string,	dsttype,
    string,	dstloc,
    DateTime,	creation_time)

sql_create_3(cg_inputs, 1, 3,
    string, 	id,
    string, 	localname,
    string, 	path);

sql_create_3(cg_outputs, 1, 3,
    string,	id,
    string, 	localname,
    string, 	path);

sql_create_3(cg_algqueue, 1, 3,
    string,	dsttype,
    string,	alg,
    string,	statistics);

#endif
