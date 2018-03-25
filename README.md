# appbench
This benchmark contains applications that were analyzed as a part of HeteroOS paper to understand the 
impact of heterogeneous memory (with different performance characteristics) on cloud-based applications.
The applications were chosen from the CloudSuite benchmark and includes a mix of CPU, memory, and IO intensive 
applications. 

Current list of applications:

- graphchi - graph computing application
- Metis - in-memory map-reduce
- redis-3.0.0 - in-memory key-value store

*More applications will be added shortly*

Setup 
------

Setup environmental variables


	$ . ./setvars.sh

Generate data and install required packages

	$ ./setup.sh


Emulating heterogeneous memory 
------------------------------

*Skip this part and go to compile and run step if you do not want to modify memory access speed*

To emulate different latency and bandwidth characteristics on commodity hardware, you can 
use HP Lab's Quartz tool. Please read the documentation of Quartz tool to understand how it works!

Installing Quartz and execute the following steps:

    cd $SHARED_LIBS
    git clone https://github.com/HewlettPackard/quartz

Make sure to set the right read/write latency and bandwidth values in shared_libs/quartz/nvmemul.ini
Typical DRAM Read/Write latency - 100-150ns. By default, Quartz uses 1000ns; as a result, some applications can be really slow unless you change the read/write latency values.

    cd $APPBENCH && ./install_quartz.sh

Note: Quartz requires your kernel's headers and modules. To install headers and modules:

    sudo apt-get install linux-headers-$(uname -r)

Enable the Quartz scripts (QUARTZSCRIPTS) and APPPREFIX environmental variables in setvars.sh

     export QUARTZSCRIPTS=$SHARED_LIBS/quartz/scripts
     export APPPREFIX=$QUARTZSCRIPTS/runenv.sh


Compile and run all applications
--------------------------------

Compile all required shared libraries, allocators (Hoard), and applications

	./compile_all.sh

Run all applications

	./runapps.sh

Collect results
---------------

See all application results in output directory

	cd output

*TO DO: Adding a script to extract results of application*

