#!/bin/sh
ipcs -m | awk ' $3 == "skannan9" {print $2, $3}' | awk '{ print $1}' | while read i; do ipcrm -m $i; done

rm -f /tmp/chk*

#mpd &

