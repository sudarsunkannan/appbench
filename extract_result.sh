#!/bin/bash
echo "   "
echo "________________________"
echo "   "
APP=graphchi
grep "runtime" $OUTPUTDIR/$APP | awk '{print "Graphchi " $1 " "  $2 " sec"}'
echo "________________________"
APP=redis
echo "   "
echo "REDIS"
echo "   "
grep  -a "SET" $OUTPUTDIR/$APP 
grep  -a "GET" $OUTPUTDIR/$APP
echo "________________________"
APP=Metis
echo "   "
grep "Real:" $OUTPUTDIR/$APP | awk '{print "Metis runtime: "  $2 " msec"}'
echo "________________________"
APP=leveldb
echo "   "
grep "micros/op" $OUTPUTDIR/$APP
echo "________________________"
APP=graph500
echo
echo "GRAPH500"
echo

awk '
  /Elapsed \(wall clock\) time \(h:mm:ss or m:ss\):/ {
    t=$0; sub(/^.*: /,"",t); gsub(/^ +| +$/,"",t)
    n=split(t,a,":")
    if (n==3) {h=a[1]; m=a[2]; s=a[3]} else {h=0; m=a[1]; s=a[2]}
    gsub(",",".",s)
    secs = h*3600 + m*60 + s
    last = secs
  }
  END { if (last!="") printf("Graph500 elapsed: %.3f sec\n", last) }
' "$OUTPUTDIR/$APP"
