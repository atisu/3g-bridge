#ifndef QUEUEMANAGER_H
#define QUEUEMANAGER_H

#include "Job.h"
#include "common.h"
#include "AlgQueue.h"
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

class QueueManager {
public:
    QueueManager(GKeyFile *config);
    ~QueueManager();
    void run();
private:
    map<string, AlgQueue *> algs;
    string basedir;
    vector<GridHandler *> gridHandlers;
    unsigned selectSize(AlgQueue *algQ);
    unsigned selectSizeAdv(AlgQueue *algQ);
    bool runHandler(GridHandler *handler);
};

#endif  /* QUEUEMANAGER_H */
