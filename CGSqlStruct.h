#include <mysql++.h>
#include <custom.h>
#include <string>

using namespace std;

sql_create_6(cg_job, 1, 6,
    int,	id,
    string, 	name,
    string, 	args,
    string,	algname,
    string,	status,
    string,	wuid)

sql_create_4(cg_inputs, 1, 4,
    int, 	id,
    string, 	localname,
    string, 	path,
    int,	jobid);

sql_create_4(cg_outputs, 1, 4,
    int,	id,
    string, 	localname,
    string, 	path,
    int, 	jobid);


