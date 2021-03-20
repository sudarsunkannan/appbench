#!/bin/bash
export CODEBASE=$PWD
export APPBENCH=$PWD
export SHARED_DATA=$APPBENCH/shared_data
export SHARED_LIBS=$APPBENCH/shared_libs
export OUTPUTDIR=$APPBENCH/output
export GRAPHCHI_ROOT=$APPBENCH/graphchi/graphchi-cpp
mkdir $OUTPUTDIR
export QUARTZSCRIPTS=$SHARED_LIBS/quartz/scripts
export APPPREFIX=""
#export APPPREFIX=$QUARTZSCRIPTS/runenv.sh
