#!/bin/sh

#JOBID=10374399
INPUTPATH=/var/lib/boinc/szdgr/cg_master
LIMIT=100

# Step 1
# DB read

#Step 2
cd $INPUTPATH/genmaster_convxml/outputs
../convert -idbs ../inputs/wkf1-input.xml -ox

for file in `ls -1`
do
(    
    cd $INPUTPATH/genmaster_convxml/outputs
    JOBID=${file%.*.*}
    cd $INPUTPATH/workdir
    
    # Step 3
    CMOL3D=`./injector -j "$JOBID.2D" -a cmol3d -c "-p params.2D3D $JOBID.2D.mol" -i params.2D3D,$JOBID.2D.mol -p $INPUTPATH/genmaster_2d3d/inputs/params.2D3D,$INPUTPATH/genmaster_convxml/outputs/$JOBID.2D.mol -o $JOBID.3D.mol`

    # Step 4
    CONFGEN=`./injector -j "$JOBID.3D" -a cmol3d -c "-p params.conf $JOBID.3D.mol" -i params.conf,$JOBID.3D.mol -p $INPUTPATH/genmaster_confgen/inputs/params.conf,$CMOL3D -o $JOBID.conf.sd`

    # Step 5
    cd $INPUTPATH/genmaster_convsd/inputs
    `cp $CONFGEN ./`
    ../convert -imdl $JOBID.conf.sd -ox
    `mv *.mol ../outputs`
    cd $INPUTPATH/workdir
        
    for (( i=1; i<=LIMIT; i++ ))
    do
	(
	    # Step 6
	    if [ -f $INPUTPATH/genmaster_convsd/outputs/$JOBID.$i.mol ];
	    then
		`./injector -j "$JOBID.$i.mol" -a mopac -c "-p params.mopac $JOBID.$i" -i params.mopac,$JOBID.$i.mol -p $INPUTPATH/genmaster_mopac/inputs/params.mopac,$INPUTPATH/genmaster_convsd/outputs/$JOBID.$i.mol -o $JOBID.$i.mop -w 0`
	    fi
	) &
    done

    i=1
    while (( i<100 ))
    do
	for (( j=1; j<LIMIT; j++ ))
	do
	    `cd $INPUTPATH/cg_master/workdir/mopac`
	    for file in `ls -1`
	    do
		`cd $file`
		if [ -f $JOBID.$i.mop ];
		then
		    TMPPATH=`pwd`
		    # Step 7
		    `../../injector -j "$JOBID.$i.desc" -a moldesccalc -c "-p params.mdc $JOBID.$i" -i params.mdc,$JOBID.$i.mol,$JOBID.$i.mop -p $INPUTPATH/genmaster_moldesccalc/inputs/params.mdc,$INPUTPATH/genmaster_convsd/outputs/$JOBID.$i.mol,$TMPPATH/$JOBID.$i.mop -o $JOBID.$i.desc -w 0`
		    (( i+= 1 ))
		fi
		`cd ..`
	    done
	done	
	sleep 10
    done
    

) &
done

exit

