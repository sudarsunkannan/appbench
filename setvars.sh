#!/bin/bash
export CODEBASE=$PWD
export APPBENCH=$PWD
export SHARED_DATA=$PWD/shared_data
export SHARED_LIBS=$PWD/shared_libs
export OUTPUTDIR=$CODEBASE/output
export GRAPHCHI_ROOT=$CODEBASE/graphchi/graphchi-cpp
mkdir $OUTPUTDIR
export QUARTZSCRIPTS=$SHARED_LIBS/quartz/scripts
export APPPREFIX=""
#export APPPREFIX=$QUARTZSCRIPTS/runenv.sh
