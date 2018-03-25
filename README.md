# appbench

A mix of CPU, memory, and IO intensive benchmark which includes:
- graphchi - graph computing application
- Metis - in-memory map-reduce
- redis-3.0.0 - in-memory key-value store

# Setup 

Setup environmental variables


	$ . ./setvars.sh

Generate data and install required packages

	$ ./setup.sh


# Compile and Run all applications

Compile all applications

	$ ./compile_all.sh

Run all apps

	$ ./runapps.sh

# Collect results

See all the application results in output directory

	$ cd output
