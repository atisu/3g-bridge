//gsoap ns service name: gggbridge
//gsoap ns service style: rpc
//gsoap ns service encoding: encoded
//gsoap ns service port: http://localhost:8080
//gsoap ns service namespace: urn:gggbridge

struct fnameArray
{
	std::string *__ptr;
	int __size;
	int __offset;
};

struct ns__Job
{
	std::string alg;
	std::string grid;
	std::string args;
	struct fnameArray *inputs;
	struct fnameArray *outputs;
};

int ns__addJob(struct ns__Job job, std::string &jobid);
int ns__getStatus(std::string jobid, std::string &status);
int ns__cancel(std::string jobid, struct ns__cancelResponse {} *out);
int ns__getOutput(std::string jobid, struct ns__getOutputResponse {} *out);
