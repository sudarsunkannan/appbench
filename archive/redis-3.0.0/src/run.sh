#!/bin/bash
set -x 

APPBASE=$CODEBASE/redis-3.0.0/src
APP=$APPBASE/pagerank
PARAM=$1
OUTPUT=$2

FlushDisk()
{
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
        sudo sh -c "sync"
        sudo sh -c "sync"
        sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

rm *.rdb
killall redis-server
sleep 5
$APPPREFIX $APPBASE/redis-server $CODEBASE/redis-3.0.0/redis.conf  &
sleep 5
$APPPREFIX $APPBASE/redis-benchmark -r 500000 -n 2000000 -c 50 -t get,set -P 16 -q  -h 127.0.0.1 -p 6500 -d 2048 &> $OUTPUT
killall redis-server





