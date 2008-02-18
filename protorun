#!/bin/sh

JOBID=10374399
INPUTPATH=/var/lib/boinc/szdgr/cg_master

./injector -j "2D3D Converter Job 1" -a 2d3dconv -c "-p params.2D3D $JOBID.2D.mol" -i params.2D3D,$JOBID.2D.mol -p $INPUTPATH/params.2D3D,$INPUTPATH/$JOBID.2D.mol -o $JOBID.3D.mol > ./out.txt
export OUTPUT=`cat ./out.txt`

./injector -j "Conformer Generator Job 1" -a flexmol -c "-p params.conf $JOBID.mol" -i params.conf,$JOBID.3D.mol -p $INPUTPATH/params.conf,$OUTPUT -o $JOBID.conf.sd > ./out.txt
export OUTPUT=`cat ./out.txt`
