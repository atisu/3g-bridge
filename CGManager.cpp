#include <iostream>
#include <uuid/uuid.h>

#include "CGAlg.h"
#include "CGQueueManager.h"

using namespace std;

int main(int argc, char **argv)
{
    try {
      // Create a Queue Manager
      CGQueueManager qm(argv[1], "boinc_szdgr", "0", "boinc-szdgr", "VfxVqw0PHT");
    
      // Create algorithms
      CGAlg _2d3dConv("2d3dconv", CG_ALG_GRID);
      CGAlg flexmol("flexmol", CG_ALG_GRID);
      CGAlg mopac("mopac", CG_ALG_GRID);      
      CGAlg molDescCalc("moldesccalc", CG_ALG_GRID);

      // Add the algorithm to the Queue Manager
      // The QM creates the Algorithm Queue
      qm.addAlg(_2d3dConv);
      qm.addAlg(flexmol);
      qm.addAlg(mopac);
      qm.addAlg(molDescCalc);

      vector<uuid_t *> *IDs = new vector<uuid_t *>;
      vector<uuid_t *> *tempIDs;
      vector<CGJob *> *jobs;
      
      while (true) {
        jobs = qm.getJobsFromDb();
	tempIDs = qm.addJobs(*jobs);
	for (vector<uuid_t *>::iterator it = tempIDs->begin(); it != tempIDs->end(); it++)
	    IDs->push_back(*it);
	// Query database for newly returned results
	qm.query(5);
      }
    } catch (int Error) {
       if (Error == DC_initMasterError  ||
           Error == DC_createWUError    || 
	   Error == DC_addWUInputError  ||
	   Error == DC_addWUOutputError ||
	   Error == DC_submitWUError) {
           cerr << "DC-API has encountered an error, see DC-API logs for specific error messages" << endl;
	   return -1;
       } else {
           cerr << "Unknown exception has occured" << endl;
	   return -1;
       }
    }
    return 0;
}
