#!/bin/bash

NPROC=32

FlushDisk()
{
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

FlushDisk

$APPPREFIX mpiexec -n $NPROC $APPBENCH/gtc-benchmark/gtc
FlushDisk
