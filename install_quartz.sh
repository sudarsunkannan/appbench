#!/bin/bash
set -x
BASE=$CODEBASE

cd $SHARED_LIBS/quartz
mkdir build
cd build
cmake ..
make clean all
