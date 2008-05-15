#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Logging.h"
#include "CGQueueManager.h"

using namespace std;

Logging *Logging::logco;
ostream *Logging::tos;
vector<CGAlgQueue *> CGAlgQueue::queues;

int main(int argc, char **argv)
{
    Logging log = Logging::getInstance(cout, DEB);
    try {
	LOG(DEB, "Creating Queue Manager");
	CGQueueManager qm(string(argv[1]), "boinc_szdgr", "0", "boinc-szdgr", "VfxVqw0PHT");

	LOG(DEB, "Starting Queue Manager");
	qm.run();
    } catch (string error) {
	LOG(CRIT, "Caught the exception: " + error);
	return -1;
    }
    return 0;
}
