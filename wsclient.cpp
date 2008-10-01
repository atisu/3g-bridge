#include "soap/soapH.h"
#include "soap/gggbridge.nsmap"

#include <vector>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>


using namespace std;


int main(int argc, char **argv)
{
	char **fdatas = NULL;
	string retval;
	struct soap soap;
	struct ns__Job job;
	struct fnameArray ins, outs;
	string instrings, outstrings;

	job.alg = argv[1];
	job.grid = argv[2];
	job.args = argv[3];
	instrings = argv[4];
	outstrings = argv[5];
	ins.__size = 0;
	ins.__ptr = NULL;
	outs.__size = 0;
	outs.__ptr = NULL;
	job.inputs = &ins;
	job.outputs = &outs;

	vector<string> inputs;
	string::size_type lastPos = instrings.find_first_not_of(",", 0);
	string::size_type pos = instrings.find_first_of(",", lastPos);
	while (string::npos != pos || string::npos != lastPos)
	{
	        inputs.push_back(instrings.substr(lastPos, pos - lastPos));
	        lastPos = instrings.find_first_not_of(",", pos);
	        pos = instrings.find_first_of(",", lastPos);
	}
	ins.__size = inputs.size();

	if (ins.__size)
	{
		fdatas = new char *[ins.__size];
		ins.__ptr = new string[ins.__size];
		if (!fdatas || !ins.__ptr)
		{
			cerr << "Failed to allocate pointer array!" << endl;
			return -1;
		}
	}

	soap_init(&soap);
	soap_set_mime(&soap, NULL, NULL);

	for (int i = 0; i < inputs.size(); i++)
	{
		string full(inputs.at(i));
		string::size_type position = full.find("=");
		if (position == string::npos)
		{
			cerr << "Wrong format of string \"" << full << "\"." << endl;
			exit(-1);
		}
		string fsp = full.substr(0, position);
		string ln = full.substr(position+1);

		struct stat sstat;
		if (-1 == stat(fsp.c_str(), &sstat))
			perror("stat failed: ");
		fdatas[i] = new char[sstat.st_size];
		if (!fdatas[i])
		{
			cerr << "Failed to allocate file data storage!" << endl;
			return -1;
		}
		int ff = open(fsp.c_str(), O_RDONLY);
		if (-1 == ff)
			perror("open failed: ");
		if (-1 == read(ff, fdatas[i], sstat.st_size))
			perror("read failed: ");
		close(ff);
		ins.__ptr[i] = strdup(fsp.c_str());
		soap_set_mime_attachment(&soap, fdatas[i], sstat.st_size, SOAP_MIME_BINARY, "application/octet-stream", NULL, NULL, ln.c_str());
	}

	if (!soap_call_ns__addJob(&soap, "http://sl4.xen:8080", NULL, job, retval))
	{
		soap_print_fault(&soap, stdout);
	}

        struct soap_multipart *attachment;
        for (attachment = soap.mime.list; attachment; attachment = attachment->next)
        {
		cout << (*attachment).size << endl;
	}

	cout << "Uj job ID: " << retval << endl;
}
