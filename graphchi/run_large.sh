#!/bin/bash
set -x
DATA=com-friendster.ungraph.txt
INPUT=$SHARED_DATA/$DATA
APPBASE=$CODEBASE/graphchi/graphchi-cpp/bin/example_apps
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

GETDATASET() {
cd $APPBENCH/$SHARED_DATA
datafiles=($APPBASE/$SHARED_DATA/com-friendster*)
if [[ ${#datafiles[@]} -gt 0 ]]; then
        wget $GRAPHDATAURL/com-friendster.ungraph.txt.gz
        gzip -d com-friendster.ungraph.txt.gz
fi
}

GETDATASET
cd $CODEBASE/graphchi 
FlushDisk
echo "edgelist" | $APPPREFIX $APP file $INPUT niters 8
set +x

