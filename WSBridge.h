//gsoap ns service name: gggbridge
//gsoap ns service style: rpc
//gsoap ns service encoding: literal
//gsoap ns service port: http://localhost:8080
//gsoap ns service namespace: urn:gggbridge
#import "stl.h"

struct ns__LogicalFile
{
	std::string logicalName;
	std::string URL;
};

struct ns__Job
{
	// Name of the already registered algorithm
	std::string alg;
	// Name of the grid plugin instance to submit to
	std::string grid;
	// List of command-line arguments
	std::vector<std::string> *args;
	// List of input files
	std::vector<struct ns__LogicalFile> *inputs;
	// List of output file names
	std::vector<std::string> *output;
};

enum ns__JobStatus { UNKNOWN, INIT, RUNNING, FINISHED, ERROR, CANCEL };

struct ns__JobOutput
{
	std::string jobid;
	std::vector<struct ns__LogicalFile> *outputs;
};

int ns__addJob(std::vector<struct ns__Job> *jobs, std::vector<std::string> *jobid);
int ns__getStatus(std::vector<std::string> *jobids, std::vector<enum ns__jobStatus> *statuses);
int ns__cancel(std::vector<std::string> *jobids, struct ns__cancelResponse {} *out);
int ns__getOutput(std::vector<std::string> *jobids, std::vector<struct ns__JobOutput> *out);
