#include <mysql++.h>
#include <custom.h>

#include <string>

sql_create_3(cg_job, 1, 3,
    int,	id,
    string, 	name,
    string,	algname)

sql_create_4(cg_inputs, 1, 4,
    int, 	id,
    string, 	localname,
    string, 	path,
    int,	jobid)

sql_create_4(cg_outputs, 1, 4,
    int,	id,
    string, 	localname,
    string, 	path,
    int, 	jobid)
