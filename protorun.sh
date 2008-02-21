#!/bin/sh

JOBID=10374399
INPUTPATH=/var/lib/boinc/cancergrid/master

# Step 1
# DB read

#Step 2
# To be executed locally

# Step 3
CMOL3D=`./injector -j "2D3D Conv Job 1" -a cmol3d -c "-p params.2D3D $JOBID.2D.mol" -i params.2D3D,$JOBID.2D.mol -p $INPUTPATH/genmaster_2d3d/inputs/params.2D3D,$INPUTPATH/genmaster_2d3d/inputs/$JOBID.2D.mol -o $JOBID.3D.mol`
echo "Output of 2D3D Converter is $CMOL3D"

# Step 4
CONFGEN=`./injector -j "Conf Gen Job 1" -a cmol3d -c "-p params.conf $JOBID.mol" -i params.conf,$JOBID.3D.mol -p $INPUTPATH/genmaster_confgen/inputs/params.conf,$CMOL3D -o $JOBID.conf.sd`
echo "Output of Conformer Generator is $CONFGEN"

# Step 5
# To be executed locally
echo "Skipping SD->MOL Converter"

# Step 6
MOPAC=`./injector -j "Mopac Job 1" -a mopac -c "-p params.mopac $JOBID.1" -i params.mopac,$JOBID.1.mol -p $INPUTPATH/genmaster_mopac/inputs/params.mopac,$INPUTPATH/genmaster_mopac/inputs/$JOBID.1.mol -o $JOBID.1.mop`
echo "Output of Mopac is $MOPAC"

# Step 7
MOLDESCCALC=`./injector -k "Mol Desc Calc Job 1" -a moldesccalc -c "-p params.mdc $JOBID.1" -i params.mdc,$JOBID.1.mol,$JOBID.1.mop -p $INPUTPATH/genmaster_moldesccalc/inputs/params.mdc,$INPUTPATH/genmaster_moldesccalc/inputs/$JOBID.1.mol,$MOPAC -o $JOBID.1.desc`
echo "Output of Molecule Descriptor Calculator is $MOLDESCCALC"

echo "End of prototype run"

