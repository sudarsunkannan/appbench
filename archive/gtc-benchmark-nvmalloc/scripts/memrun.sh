#!/bin/bash

NPROC=36
DMESGREADER="$HOME/ssd/NVM/appbench/apps/NPB3.4/NPB3.4-MPI/scripts/readdmesg.py"

rm -rf DATA_RESTART*

LD_PRELOAD=/usr/lib/libmigration.so /usr/bin/time -v mpiexec -NP 36 ./gtc &

$DMESGREADER init
while :
do
	sleep 1
	if pgrep -x "mpiexec" >/dev/null
	then
		$DMESGREADER readfrom Cum_mem-unlimited.csv
	else
		break
	fi
done

rm -rf DATA_RESTART*
