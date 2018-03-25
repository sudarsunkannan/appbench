DATA=com-orkut.ungraph.txt
INPUT=$SHARED_DATA/$DATA
BASE=$CODEBASE/phoenix-2.0
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

cd $CODEBASE/graphchi 
FlushDisk
echo "edgelist" | $APPPREFIX $APP file $INPUT niters 8 

