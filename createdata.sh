#!/bin/bash

#get data
mkdir $APPBASE/$SHARED_DATA
cd $APPBASE/$SHARED_DATA

if [ ! -f com-orkut.ungraph.txt ]; then
        wget https://snap.stanford.edu/data/bigdata/communities/com-orkut.ungraph.txt
fi

if [ ! -f crime2GB.data ]; then
	wget -O crime2GB.data https://norvig.com/big.txt
	for i in {1..8}; do cat crime2GB.data crime2GB.data > crime4GB.data && mv crime4GB.data crime2GB.data ; done && rm crime4GB.data
fi
