#!/bin/sh
DATA=$SHARED_DATA/crime2GB.data
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


LoadtoRamDisk()
{
  #remout ramdisk
  sudo umount /tmp/ramdisk/
  ~/codes/nvmalloc/nvkernel_test_code/ramdisk_create.sh 2048
  cp $SHARED_DATA/$DATA /tmp/ramdisk
  SHARED_DATA=/tmp/ramdisk
}


cd $APPBASE
FlushDisk
#PerformMigration
#LD_PRELOAD=/usr/lib/libmigration.so 
/usr/bin/time -v $APP $DATA 
exit
