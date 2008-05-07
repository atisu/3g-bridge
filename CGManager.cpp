#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <uuid/uuid.h>

#include "CGAlg.h"
#include "CGQueueManager.h"

using namespace std;

int main(int argc, char **argv)
{
    try {
	// Create a Queue Manager
	CGQueueManager qm(string(argv[1]), "boinc_szdgr", "0", "boinc-szdgr", "VfxVqw0PHT");

	// Create algorithms
	CGAlg cmol3d("cmol3d", CG_ALG_DCAPI);
	CGAlg mopac("mopac", CG_ALG_DCAPI);
	CGAlg molDescCalc("moldesccalc", CG_ALG_DCAPI);

	// Add the algorithm to the Queue Manager
	// The QM creates the Algorithm Queue
	qm.addAlg(cmol3d);
	qm.addAlg(mopac);
	qm.addAlg(molDescCalc);

	qm.run();
    } catch (string error) {
	cerr << "Thrown exception: " << error << endl;
	return -1;
    }
    return 0;
}
