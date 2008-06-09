#ifndef __CGQUEUEMANAGER_H
#define __CGQUEUEMANAGER_H

#include "CGJob.h"
#include "common.h"
#include "CGAlgQueue.h"
#include "GridHandler.h"

#include <glib.h>

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace std;

enum jobOperation {
  submit,
  status,
  cancel
};

class CGQueueManager {
public:
    CGQueueManager(GKeyFile *config);
    ~CGQueueManager();
    void run();
private:
    map<string, CGAlgQueue *> algs;
    string basedir;
    vector<GridHandler *> gridHandlers;
    unsigned selectSize(CGAlgQueue *algQ);
    unsigned selectSizeAdv(CGAlgQueue *algQ);
    bool runHandler(GridHandler *handler);
};

#endif  /* __CGQUEUEMANAGER_H */
