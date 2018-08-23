# appbench
This benchmark contains applications that were analyzed as a part of HeteroOS paper to understand the 
impact of heterogeneous memory (with different performance characteristics) on cloud-based applications.
The applications were chosen from the CloudSuite benchmark and includes a mix of CPU, memory, and IO intensive 
applications. 

Current list of applications:

- Graphchi - graph compute engine
- Metis - in-memory map-reduce
- Redis - in-memory key-value store

*More applications will be added shortly*

Setup 
------

Setup environmental variables
```
	$ . ./setvars.sh
```

Generate data and install required packages
```
	$ ./setup.sh
```

Compile and run all applications
--------------------------------
Compile all required shared libraries, allocators (Hoard), and applications
```
	./compile_all.sh
```
Run all applications
```
	./runapps.sh
```

Collect results
---------------

Application results are in the output directory. To simply extract results, use the following command:
```
        $APPBENCH/extract_result.sh
```
For application-specific output information, just look at the output file including errors in execution.


Emulating heterogeneous memory 
------------------------------
Skip this part and go to compile and run step if you do not want to modify memory access speed*

To emulate different latency and bandwidth characteristics on commodity hardware, you can 
use HP Lab's Quartz tool. Please read the documentation of Quartz tool to understand how it works!

Installing Quartz and execute the following steps:
```
    sudo apt-get install libconfig-dev libmpich-dev uthash-dev
    cd $SHARED_LIBS
    git clone https://github.com/SudarsunKannan/quartz
```

Run the throttling setup script 
```
    $APPBENCH/throttle.sh 
```
        
If something goes wrong, then run them manually as shown below

Step 1: Make sure to set the right read/write latency and bandwidth values in shared_libs/quartz/nvmemul.ini
Typical DRAM read/write latency is around 100ns. By default, Quartz uses 1000ns; as a result, some applications 
can be really slow unless you change the read/write latency values (say 200ns)
```
    cd $APPBENCH && ./install_quartz.sh
```

Note: Quartz requires your kernel's headers and modules. To install headers and modules:
```
    sudo apt-get install linux-headers-$(uname -r)
```

Step 2: Enable the Quartz scripts (QUARTZSCRIPTS) and APPPREFIX environmental variables in setvars.sh
```
     export QUARTZSCRIPTS=$SHARED_LIBS/quartz/scripts
     export APPPREFIX=$QUARTZSCRIPTS/runenv.sh
     # First time to generate the bandwidth model and PCI model of your machine
     $APPPREFIX /bin/ls
     
    # Now copy the files to your APPBENCH folder
    cp /tmp/bandwidth_model $APPBENCH/
    cp /tmp/mc_pci_bus $APPBENCH/
```

Step 3: For subsequent runs, you can copy the bandwidth and the PCI file to /tmp to avoid generating them again

Step 4: For modifying bandwidth, open the following file 
```
     vim $APPBENCH/shared_libs/quartz/nvmemul.ini	
```

Change the read and write to same bandwidth values
```
	bandwidth:
	{
	    enable = true;
	    model = "/tmp/bandwidth_model";
	    read = 5000;
	    write = 5000;
	};
   ```  




   

