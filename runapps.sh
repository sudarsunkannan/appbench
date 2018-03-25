#!/bin/bash


BASE=$CODEBASE
cd $BASE
RUNNOW=1
mkdir $OUTPUTDIR
rm $OUTPUTDIR/*

USAGE(){
echo "./app \$maxhotpage \$BW \$outputdir \$app"
}



RUNAPP(){
  cd $APPBASE
  $APPBASE/run.sh $RUNNOW $OUTPUTDIR/$APP &> $OUTPUTDIR/$APP
}


#if [ -z "$1" ]
# then	
#  USAGE 
#  exit
#fi



if [ -z "$4" ]
  then

	APPBASE=$BASE/Metis
	APP=Metis
	echo "running $APP..."
	RUNAPP

	exit

	APPBASE=$BASE/graphchi
	APP=graphchi
	echo "running $APP ..."
	RUNAPP
	/bin/rm -rf com-orkut.ungraph.txt.*

	APPBASE=$BASE/redis-3.0.0/src
	APP=redis
	echo "running $APP..."
	RUNAPP

	exit

	#APPBASE=$BASE/memcached
	#APP=memcached
	#echo "running $APP ..."
	#RUNAPP

	APPBASE=$BASE/xstream_release
	APP=xstream_release
	scp -r $HOSTIP:$SHARED_DATA*.ini $APPBASE
        cp $APPBASE/*.ini $SHARED_DATA
	echo "running $APP ..."
	RUNAPP

	APPBASE=$BASE/leveldb
	APP=leveldb
	echo "running $APP..."
	RUNAPP
fi
	
#APPBASE=$BASE/$4
#APP=$4
#RUNAPP

