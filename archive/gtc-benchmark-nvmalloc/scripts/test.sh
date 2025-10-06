NPROC=16
#APPPREFIX="numactl --membind=0"

rm -rf DATA_RESTART*
../../scripts/clear_cache.sh

#export LD_PRELOAD=/usr/lib/libmigration.so 
export LD_PRELOAD=/usr/lib/libcfun.so

export IOMODE=ASYNC
export FILETYPE=UNIQUE

/usr/bin/time -v mpiexec -n $NPROC ./gtc
#$APPPREFIX /usr/bin/time -v mpiexec -n $NPROC ./MADbench2.x 2400 140 1 8 8 4 4

