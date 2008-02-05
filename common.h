#ifndef __CG_COMMON_H
#define __CG_COMMON_H

enum CGJobStatus {
    CG_INIT,
//    CG_SUBMITTED, Same as running
    CG_RUNNING,
    CG_FINISHED,
    CG_ERROR
};

const int DC_initMasterError = -1;
const int DC_createWUError = -2;
const int DC_addWUInputError = -3;
const int DC_addWUOutputError = -4;
const int DC_submitWUError = -5;

#endif  /* __CG_COMMON_H */
