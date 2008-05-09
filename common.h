#ifndef __CG_COMMON_H
#define __CG_COMMON_H

enum CGJobStatus {
    INIT,
    RUNNING,
    FINISHED,
    ERROR,
    CANCEL,
};

const int DC_initMasterError = -1;
const int DC_createWUError = -2;
const int DC_addWUInputError = -3;
const int DC_addWUOutputError = -4;
const int DC_submitWUError = -5;

#endif  /* __CG_COMMON_H */
