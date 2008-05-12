#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <uuid/uuid.h>

#include "CGAlgQueue.h"
#include "CGQueueManager.h"

using namespace std;

vector<CGAlgQueue *> CGAlgQueue::queues;

int main(int argc, char **argv)
{
    try {
	// Create a Queue Manager
	CGQueueManager qm(string(argv[1]), "boinc_szdgr", "0", "boinc-szdgr", "VfxVqw0PHT");

	qm.run();
    } catch (string error) {
	cerr << "Thrown exception: " << error << endl;
	return -1;
    }
    return 0;
}
