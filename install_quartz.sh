#!/bin/bash
set -x
BASE=$CODEBASE

cd $SHARED_LIBS/quartz
mkdir build
cd build
cmake ..
make clean all
sudo $SHARED_LIBS/quartz/scripts/setupdev.sh load
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
echo 2 | sudo tee /sys/devices/cpu/rdpmc
