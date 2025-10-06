#!/bin/bash
#set -x

APPDIR=$PWD
cd $APPDIR
declare -a caparr=("22483")
declare -a thrdarr=("36")
declare -a workarr=("100")
declare -a apparr=("GTC")

#APPPREFIX="numactl --membind=0"
APPPREFIX=""

#Make sure to compile and install perf
USEPERF=0
PERFTOOL="$HOME/ssd/NVM/linux-stable/tools/perf/perf"

DMESGREADER="$HOME/ssd/NVM/appbench/apps/NPB3.4/NPB3.4-MPI/scripts/readdmesg.py"

SLEEPNOW() {
	sleep 2
}


SETPERF() {

	sudo sh -c "echo 0 > /proc/sys/kernel/perf_event_paranoid"
	sudo sh -c "echo 0 > /proc/sys/kernel/kptr_restrict"
	SLEEPNOW
}


#Mount ramdisk to reserve memory and reduce overall memory availability
SETUPEXTRAM() {

	let CAPACITY=$1

	let SPLIT=$CAPACITY/2
	echo "SPLIT" $SPLIT

        sudo rm -rf  /mnt/ext4ramdisk0/*
        sudo rm -rf  /mnt/ext4ramdisk1/*

	./umount_ext4ramdisk.sh 0
	./umount_ext4ramdisk.sh 1

        SLEEPNOW

        NUMAFREE0=`numactl --hardware | grep "node 0 free:" | awk '{print $4}'`
        NUMAFREE1=`numactl --hardware | grep "node 1 free:" | awk '{print $4}'`

        let DISKSZ=$NUMAFREE0-$SPLIT
        let ALLOCSZ=$NUMAFREE1-$SPLIT

        echo "NODE 0 $DISKSZ NODE 1 $ALLOCSZ"

        ./mount_ext4ramdisk.sh $DISKSZ 0
        ./mount_ext4ramdisk.sh $ALLOCSZ 1

	SLEEPNOW
}


#Here is where we run the application
RUNAPP() 
{
	#Run application
	cd $APPDIR
	mkdir results-sensitivity

	CAPACITY=$1
	NPROC=$2
	WORKLOAD=$3
	APP=$4
	OUTPUT=results-sensitivity/"MEMSIZE-$WORKLOAD-"$NPROC"threads-"$CAPACITY"M.out"


	if [[ $USEPERF == "1" ]]; then
		SETPERF
		APPPREFIX="sudo $PERFTOOL record -e cpu-cycles,instructions --vmlinux=/lib/modules/4.17.0/build/vmlinux "
	else
		APPPREFIX="/usr/bin/time -v"
	fi

	if [ "$APP" = "MADbench" ]; then
		$APPPREFIX mpiexec -n $NPROC ./MADbench2_io $WORKLOAD 140 1 8 8 4 4 &> $OUTPUT
	fi
	if [ "$APP" = "GTC" ]; then
		rm -rf DATA_RESTART*
		#export LD_PRELOAD=/usr/lib/libmigration.so
		$APPPREFIX mpiexec -n $NPROC ./gtc
		rm -rf DATA_RESTART*
		$APPPREFIX mpiexec -n $NPROC ./gtc
		export LD_PRELOAD=""
		#$DMESGREADER init
		#while :
		#do
		#	sleep 1
		#	if pgrep -x "mpiexec" >/dev/null
		#	then
		#		$DMESGREADER readfrom Cum_mem-$CAPACITY.csv
		#	else
		#		break
		#	fi
		#done

	fi
}


#Do all things during termination
TERMINATE() 
{
	CAPACITY=$1
	NPROC=$2
	WORKLOAD=$3
	
	OUTPUT=results-sensitivity/"PERF-MEMSIZE-$WORKLOAD-"$NPROC"threads-"$CAPACITY"M.out"

	if [[ $USEPERF == "1" ]]; then
		SLEEPNOW
		sudo $PERFTOOL report &>> $OUTPUT
		sudo $PERFTOOL report --sort=dso &>> $OUTPUT
	fi
}


for APP in "${apparr[@]}"
do
	for CAPACITY  in "${caparr[@]}"
	do 
		SETUPEXTRAM $CAPACITY

		for NPROC in "${thrdarr[@]}"
		do	
			for WORKLOAD in "${workarr[@]}"
			do
				RUNAPP $CAPACITY $NPROC $WORKLOAD $APP
				SLEEPNOW
				./clear_cache.sh
				TERMINATE $CAPACITY $NPROC $WORKLOAD
			done 
		done	
	done
done
