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
      CGQueueManager qm(argv[1], "boinc_szdgr", "0", "boinc-szdgr", "VfxVqw0PHT");
    
      // Create algorithms
      CGAlg cmol3d("cmol3d", CG_ALG_GRID);
      CGAlg mopac("mopac", CG_ALG_GRID);
      CGAlg molDescCalc("moldesccalc", CG_ALG_GRID);

      // Add the algorithm to the Queue Manager
      // The QM creates the Algorithm Queue
      qm.addAlg(cmol3d);
      qm.addAlg(mopac);
      qm.addAlg(molDescCalc);

      vector<uuid_t *> *IDs = new vector<uuid_t *>;
      vector<CGJob *> *jobs;

      // Reinitialize old jobs
      jobs = qm.getJobsFromDb(true);
      vector<uuid_t *> *tempIDs = qm.addJobs(*jobs, true);
      for (vector<uuid_t *>::iterator it = tempIDs->begin(); it != tempIDs->end(); it++)
        IDs->push_back(*it);

      while (true) {
        // Get new jobs from database
        jobs = qm.getJobsFromDb();
        vector<uuid_t *> *tempIDs = qm.addJobs(*jobs);
        for (vector<uuid_t *>::iterator it = tempIDs->begin(); it != tempIDs->end(); it++)
    	    IDs->push_back(*it);

	// Query database for newly returned results
	qm.queryBoinc(5);
	qm.putOutputsToDb();
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
