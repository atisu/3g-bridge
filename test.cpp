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
    
      CGAlg a1("flexmol", CG_ALG_DCAPI);	// Create an algorithm

      // Create a Queue Manager
      CGQueueManager qm(argv[1], "boinc_szdgr", "0", "boinc-szdgr", "VfxVqw0PHT");

//      vector<CGJob *> jobs;
      
      vector<uuid_t *> *IDs;

      // Add the algorithm to the Queue Manager
      // The QM creates the Algorithm Queue
      qm.addAlg(a1);

      // Create some jobs belonging to Algorith type A1
//      for (int i = 0; i < 10; i++) {
//
//	nJob->addInput("INPUT1", "/tmp/INPUT.1");
//	nJob->addInput("INPUT2", "/tmp/INPUT.2");
//	nJob->addOutput("OUTPUT1");
//	nJob->addOutput("OUTPUT2");
//	jobs.push_back(nJob);
//      }

	
      // Send the jobs to the Queue Manager
    vector<CGJob *> *jobs;
    jobs = qm.getJobsFromDb();
    
//cout <<     jobs->at(0)->getInputPath("INPUT1") << endl;
//cout <<     jobs->at(0)->getOutputPath("OUTPUT1") << endl;
    
IDs = qm.addJobs(*jobs);

//    jobs = qm.getJobsFromDb();
    
//    jobs->at(0)->ge
//    IDs = qm.addJobs(jobs);
    
cout << "Jobs added to QM, IDs.size = " << IDs->size() << endl;

     qm.removeJob(IDs->at(0));
      
      return 0;
      
      // Check the number of returned IDs
      cout << IDs->size() << endl;

      // Do some status query
      qm.getStatus(IDs->at(0));
      qm.getStatus(IDs->at(0));
      qm.getStatus(IDs->at(1));
      qm.getStatus(IDs->at(0));
      qm.getStatus(IDs->at(1));
      qm.getStatus(IDs->at(2));

      for (int i = 0; i < 5; i++) {
	vector<CGJobStatus> *stats;
	stats = qm.getStatuses(*IDs);	// Get status informations
	cout << stats->size() << endl;
	int nINIT = 0, nSUBMITTED = 0, nRUNNING = 0, nFINISHED = 0, nERROR = 0, nUNKNOWN = 0;
	// Count number of different statuses
	for (vector<CGJobStatus>::iterator it = stats->begin(); it != stats->end(); it++) {
	    switch (*it) {
		case CG_INIT:
		    nINIT++;
		    break;
//		case CG_SUBMITTED:
//		    nSUBMITTED++;
//		    break;
		case CG_RUNNING:
		    nRUNNING++;
		    break;
		case CG_FINISHED:
		    nFINISHED++;
		    break;
		case CG_ERROR:
		    nERROR++;
		    break;
		default:
		    nUNKNOWN++;
		    break;
	    }
  	  }
	  cout << "INIT:      " << nINIT << endl;
//	  cout << "SUBMITTED: " << nSUBMITTED << endl;
	  cout << "RUNNING:   " << nRUNNING << endl;
	  cout << "FINISHED:  " << nFINISHED << endl;
	  cout << "ERROR:     " << nERROR << endl;
	  cout << "UNKNOWN:   " << nUNKNOWN << endl;
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
