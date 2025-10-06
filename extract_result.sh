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

echo "________________________"
APP=gtc
echo
echo "GTC"
echo

if grep -q "^TOTAL WALL CLOCK TIME(SEC):" "$OUTPUTDIR/$APP"; then
  grep "^TOTAL WALL CLOCK TIME(SEC):" "$OUTPUTDIR/$APP" | tail -n1
else
  awk '
    # Prefer: time -v line
    /Elapsed \(wall clock\) time \(h:mm:ss or m:ss\):/ {
      t=$0; sub(/^.*: /,"",t); gsub(/^ +| +$/,"",t)
      n=split(t,a,":")
      if (n==3) {h=a[1]; m=a[2]; s=a[3]} else {h=0; m=a[1]; s=a[2]}
      gsub(",",".",s)
      last = h*3600 + m*60 + s
      next
    }
    {
      l=tolower($0)

      # Fallback A: detect H:M:S or M:S anywhere
      if (match($0, /([0-9]+:)?[0-9]+:[0-9.]+/)) {
        t=substr($0,RSTART,RLENGTH)
        n=split(t,a,":")
        if (n==3) {h=a[1]; m=a[2]; s=a[3]} else {h=0; m=a[1]; s=a[2]}
        gsub(",",".",s)
        last = h*3600 + m*60 + s
        next
      }

      # Fallback B: lines mentioning total/elapsed/overall time with numeric+unit
      if (index(l,"total time") || index(l,"elapsed time") || index(l,"time elapsed") || index(l,"overall time")) {
        if (match(l, /([0-9]*\.[0-9]+|[0-9]+)[[:space:]]*(ms|milliseconds|s|sec|secs|seconds|m|min|mins|minutes|h|hr|hrs|hours)\b/)) {
          t=substr(l,RSTART,RLENGTH)
          split(t,parts," ")
          v=parts[1]+0; u=parts[2]
          if (u ~ /^ms/)      last=v/1000
          else if (u ~ /^(m|min)/) last=v*60
          else if (u ~ /^(h|hr)/)  last=v*3600
          else                    last=v
        }
      }
    }
    END { if (last!="") printf("TOTAL WALL CLOCK TIME(SEC): %.3f\n", last) }
  ' "$OUTPUTDIR/$APP"
fi
