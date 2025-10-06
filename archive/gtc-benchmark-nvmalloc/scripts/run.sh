#!/bin/bash

PCAnonRatio=1.5
#DBGRATIO=1
#DRATIO=100
#BASE_MEM=2758459392
NPROC=32
#APPPREFIX="numactl --membind=0"
APPPREFIX=""
WORKLOAD=2000
#ProgMem=`echo "74828 * $NPROC * 1024" | bc` #in bytes For size C
#TotalMem=`echo "$ProgMem * $PCAnonRatio" | bc`
#TotalMem=`echo $TotalMem | perl -nl -MPOSIX -e 'print ceil($_)'`
CAPACITY=$1

FlushDisk()
{
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

SETUPEXTRAM() {

        sudo rm -rf  /mnt/ext4ramdisk0/*
        sudo rm -rf  /mnt/ext4ramdisk1/*
	./umount_ext4ramdisk.sh 0
	./umount_ext4ramdisk.sh 1
        sleep 5
        NUMAFREE0=`numactl --hardware | grep "node 0 free:" | awk '{print $4}'`
        NUMAFREE1=`numactl --hardware | grep "node 1 free:" | awk '{print $4}'`
        let DISKSZ=$NUMAFREE0-$CAPACITY
        let ALLOCSZ=$NUMAFREE1-300
        echo $DISKSZ"*************"
        #./umount_ext4ramdisk.sh 0
        #./umount_ext4ramdisk.sh 1
        ./mount_ext4ramdisk.sh $DISKSZ 0
        ./mount_ext4ramdisk.sh $ALLOCSZ 1
}

FlushDisk
#SETUPEXTRAM
echo "going to sleep"
sleep 1

#LD_PRELOAD=/usr/lib/libcrosslayer.so  
LD_PRELOAD=$SRC_PATH/shim.so mpiexec -n $NPROC $APPBENCH/gtc-benchmark/gtc
FlushDisk
#./umount_ext4ramdisk.sh 0
#./umount_ext4ramdisk.sh 1
