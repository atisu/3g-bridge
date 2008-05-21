#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Logging.h"
#include "CGQueueManager.h"
#include "QMConfig.h"

using namespace std;

vector<CGAlgQueue *> CGAlgQueue::queues;

int main(int argc, char **argv)
{
    Logging::init(cout, LOG_DEBUG);

    if (argc < 2)
    {
	    cerr << "Missing config. file name\n";
	    exit(1);
    }
    if (argc > 2)
    {
	    cerr << "Extra arguments on the command-line\n";
	    exit(1);
    }

    QMConfig cfg(argv[1]);

    try {
	LOG(LOG_DEBUG, "Creating Queue Manager");
	CGQueueManager qm(cfg);

	LOG(LOG_DEBUG, "Starting Queue Manager");
	qm.run();
    } catch (string error) {
	LOG(LOG_CRIT, "Caught the exception: " + error);
	return -1;
    }
    return 0;
}
