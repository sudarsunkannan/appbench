#!/bin/sh
set -x
DATA=$SHARED_DATA/crime.data
BASE=$CODEBASE/Metis
APPBASE=$BASE/obj
APP=$APPBASE/wc
PARAM=$1
OUTPUT=$2

FlushDisk()
{
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}


cd $APPBASE
FlushDisk
$APPPREFIX $APP $DATA 
set +x
exit
