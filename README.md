# appbench

This benchmark suite was used in the HeteroOS study to analyze how heterogeneous memory (with different performance characteristics) impacts cloud applications. The suite mixes CPU-, memory-, and IO-intensive workloads.

## Applications

Core:
- **Graphchi** — graph compute engine (PageRank, etc.)
- **Metis** — in-memory MapReduce
- **Redis** — in-memory key-value store
- **LevelDB** — LSM key-value store
- **Graph500** — MPI BFS (TEPS benchmark)
- **GTC benchmark** — scientific kernel (MPI/OpenMP variants exist)

Optional / extras (present in repo/scripts, may be environment-dependent):
- **Phoenix 2.0** — MapReduce suite (core + `word_count`)
- **xstream_release** — helper workload (uses `.ini` configs)

> The compile / run scripts let you run **all** or **only the apps you name**.

---

## Prerequisites

- Build tools: `gcc g++ gfortran make cmake pkg-config`
- MPI (pick one): **MPICH** or **Open MPI**
- Other common libs on Ubuntu/Debian:
  ```bash
  sudo apt update
  sudo apt install build-essential git \
       mpich libmpich-dev    # or: openmpi-bin libopenmpi-dev


Setup 
------

Setup environmental variables
```
	source ./setvars.sh
```

Generate data and install required packages
```
	./setup.sh
```

Compile and run all applications
--------------------------------
# Build all shared libs and apps
```
./compile_all.sh
```

# List known app keys
```
./compile_all.sh list
```

# Build only some apps
```
./compile_all.sh graph500 leveldb
```

# Run all apps

```
./run_all.sh

# List app keys
./run_all.sh list

# Run only some apps
./run_all.sh graph500 redis

# Pin outputs to a specific directory
OUTPUTDIR=$APPBENCH/outputs/myrun ./run_all.sh graph500
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
Skip this part and go to compile and run step if you do not want to modify memory access speed

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


Graphchi: running large dataset
--------------------------------
To run page rank on a large dataset from Friendster (a now-defunct gaming social network), 
from appbench base directory, run following commands:

```
        source ./setvars.sh
        cd $APPBENCH/graphchi
        ./run_large.sh
```
NOTE: First run would involve creating graph shards which could take quite sometime before running the
page rank iterations.
