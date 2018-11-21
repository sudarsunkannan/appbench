#!/bin/bash

#get data
mkdir $APPBASE/$SHARED_DATA
cd $APPBASE/$SHARED_DATA

GRAPHDATAURL=https://snap.stanford.edu/data/bigdata/communities

if [ ! -f com-orkut.ungraph.txt ]; then
        wget $GRAPHDATAURL/com-orkut.ungraph.txt
fi

#datafiles=($APPBASE/$SHARED_DATA/com-friendster*)
#if [[ ${#datafiles[@]} -gt 0 ]]; then
	#wget $GRAPHDATAURL/com-friendster.ungraph.txt.gz
	#gzip -d com-friendster.ungraph.txt.gz
#fi

if [ ! -f crime.data ]; then
	wget -O crime.data https://norvig.com/big.txt
	for i in {1..8}; do cat crime.data crime.data > crime4GB.data && mv crime4GB.data crime.data ; done && rm crime4GB.data
fi
