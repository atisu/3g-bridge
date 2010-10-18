#!/bin/sh

VM_MAX=1
# not implemented
VM_CORE=2
VM_START_INTERVAL=8

SLEEP_INTERVAL=4
G3_DATABASE="boinc_cloud"
QUEUE_VM="Cloud"
QUEUE_JOB="BOINC"
VM_IMAGE="ami-0bcce67f"
#VM_TYPE="c1.medium"
VM_TYPE="m1.small"
VM_IDLE_SHUTDOWN=180

MYTMP=/tmp/vm_start_time
MYRAND=${RANDOM}

if [ "$1"="--clean" ] && [ -f ${MYTMP} ]; then
  rm ${MYTMP}
fi

if [ -f ${MYTMP} ]; then
 VM_START_TIME=`cat ${MYTMP}`
else
 VM_START_TIME=0
fi

LAST_RUNNING_TIME=`date +%s`

function do_log {
  echo `date  +"%Y-%m-%d %H:%M:%S"` $1":" $2
}


function start_vm {
  JOBID=`./injector -c 3g-bridge-EC2.conf -a cloud -g ${QUEUE_VM} \
    -p "--image=${VM_IMAGE} --instance-type=${VM_TYPE} --group=boinc" --input=in:in --output=out:out`
  do_log INFO "Starting a VM instance (job id: ${JOBID})"
}

function stop_all_vm {
  mysql -e "UPDATE cg_job set status='CANCEL' where grid='${QUEUE_VM}' and status<>'ERROR' " ${G3_DATABASE}
  do_log INFO "No jobs are running since ${VM_IDLE_SHUTDOWN} seconds, shutting down all VM instances"
}


while [ 1 ]; do
  RUNNING_JOBS=`echo "select count(*) from cg_job where grid='${QUEUE_JOB}' and status='RUNNING'" | mysql -s ${G3_DATABASE}`
  RUNNING_VMS=`echo "select count(*) from cg_job where grid='${QUEUE_VM}' and status='RUNNING'" | mysql -s ${G3_DATABASE}`
  INIT_JOBS=`echo "select count(*) from cg_job where grid='${QUEUE_JOB}' and status='INIT'" | mysql -s ${G3_DATABASE}`
  INIT_VMS=`echo "select count(*) from cg_job where grid='${QUEUE_VM}' and status='INIT'" | mysql -s ${G3_DATABASE}`
  
  if [ $((${RUNNING_VMS}+${INIT_VMS})) -lt ${RUNNING_JOBS} ] && \
     [ $((${RUNNING_VMS}+${INIT_VMS})) -lt ${VM_MAX} ] && \
     [ `date +%s` -gt $((${VM_START_TIME}+${VM_START_INTERVAL})) ]; then
    echo `date +%s` > ${MYTMP}
    VM_START_TIME=`cat ${MYTMP}`
    start_vm    
  fi

  if [ $((${RUNNING_VMS}+${INIT_VMS})) -gt 0 ] && \
     [ ${RUNNING_JOBS} -eq 0 ]; then
    if [ $((`date +%s`-${LAST_RUNNING_TIME})) -gt ${VM_IDLE_SHUTDOWN} ]; then
      stop_all_vm
    fi 
  else
     if [ ${RUNNING_JOBS} -gt 0 ]; then
       LAST_RUNNING_TIME=`date +%s`  
     fi
  fi

  TIMEOUT=$((${VM_START_INTERVAL}-(`date +%s`-${VM_START_TIME})))
  if [ ${TIMEOUT} -lt 0 ]; then
     TIMEOUT=0
  fi

  if [ $((${RUNNING_VMS}+${INIT_VMS})) -gt 0 ] || \
     [ ${RUNNING_JOBS} -gt 0 ]; then
     do_log INFO "${RUNNING_VMS} VMs ($((${RUNNING_VMS}*${VM_CORE})) VCPUs), and ${RUNNING_JOBS} Jobs running (${TIMEOUT} seconds before next VM start slot)"
  fi;

  sleep ${SLEEP_INTERVAL};
done
