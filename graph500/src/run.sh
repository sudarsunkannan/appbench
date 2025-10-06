#!/bin/sh
export SKIP_VALIDATION=1
echo "Running for $GRAPH500SCALE"
/usr/bin/time  -v mpiexec -n $NUM_PROCS ./graph500_reference_bfs $GRAPH500SCALE
