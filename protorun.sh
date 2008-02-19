#!/bin/sh

JOBID=10374399
INPUTPATH=/var/lib/boinc/szdgr/cg_master

# Step 1
# DB read

#Step 2
# To be executed locally

# Step 3
OUTPUT=`./injector -j "2D3D Converter Job 1" -a 2d3dconv -c "-p params.2D3D $JOBID.2D.mol" -i params.2D3D,$JOBID.2D.mol -p $INPUTPATH/params.2D3D,$INPUTPATH/$JOBID.2D.mol -o $JOBID.3D.mol`
echo "Output is $OUTPUT"

# Step 4
OUTPUT=`./injector -j "Conformer Generator Job 1" -a flexmol -c "-p params.conf $JOBID.mol" -i params.conf,$JOBID.3D.mol -p $INPUTPATH/params.conf,$OUTPUT -o $JOBID.conf.sd`
echo "Output is $OUTPUT"

# Step 5
# To be executed locally

# Step 6
OUTPUT=`./injector -j "Mopac Job 1" -a mopac -c "-p params.mopac $JOBID.1" -i params.mopac,$JOBID.1.mol -p $INPUTPATH/params.mopac,$INPUTPATH/$JOBID.1.mol -o $JOBID.1.mop`
echo "Output is $OUTPUT"

# Step 7 
OUTPUT=`./injector -k "Molecule Descriptor Calculator Job 1" -a moldesccalc -c "-p params.mdc $JOBID.1" -i params.mdc,$JOBID.1 -p $INPUTPATH/params.mdc,$INPUTPATH/$JOBID.1 -o $JOBID.1.desc`
echo "Output is $OUTPUT"

echo "End of prototype run"
