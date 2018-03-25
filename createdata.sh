#!/bin/bash

#get data
mkdir $APPBASE/$SHARED_DATA
cd $APPBASE/$SHARED_DATA

if [ ! -f com-orkut.ungraph.txt ]; then
        wget https://snap.stanford.edu/data/bigdata/communities/com-orkut.ungraph.txt
fi

if [ ! -f crime2GB.data ]; then
        base64 /dev/urandom | head -c 2000000000 > crime2GB.data
fi
