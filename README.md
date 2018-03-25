# appbench
This benchmark contains applications that were analyzed as a part of HeteroOS paper to understand the 
impact of heterogeneous memory (with different performance characteristics) on cloud-based applications.
The applications were chosen from the CloudSuite benchmark and includes a mix of CPU, memory, and IO intensive 
applications. 

Current list of applications:

- graphchi - graph computing application
- Metis - in-memory map-reduce
- redis-3.0.0 - in-memory key-value store

# Setup 

Setup environmental variables


	$ . ./setvars.sh

Generate data and install required packages

	$ ./setup.sh


# Emulating heterogeneous memory 

*Skip this part and go to compile and run step if you do not want to modify memory access speed*

To emulate different latency and bandwidth characteristics on commodity hardware, you can 
use HP Lab's Quartz tool. Please read the documentation of Quartz tool to understand how it works!

To use Quartz:

1. Download the tool to shared_libs folder

	$ cd $APPBENCH/shared_libs   (APPBENCHSRC is APPBENCH's root folder)
	$ git clone https://github.com/HewlettPackard/quartz

2. Install the tool 

	$ cd $APPBENCH && ./install_quartz.sh

3. Enable the Quartz scripts (QUARTZSCRIPTS) and APPPREFIX environmental variables in setvars.sh

	$ export QUARTZSCRIPTS=$SHARED_LIBS/quartz/scripts
	$ export APPPREFIX=$QUARTZSCRIPTS/runenv.sh



# Compile and run all applications

Compile all required shared libraries, allocators (Hoard), and applications

	$ ./compile_all.sh

Run all apps

	$ ./runapps.sh

# Collect results

See all the application results in output directory

	$ cd output




