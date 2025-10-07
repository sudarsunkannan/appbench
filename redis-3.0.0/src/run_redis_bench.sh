rm s*.out
./redis-benchmark -r 1000000 -n 2000000 -c 50 -t get,set -P 16 -q  -h 127.0.0.1 -p 6379 -d 1024 &> s1.out 
#./redis-benchmark -r 1000000 -n 2000000 -c 50 -t get,set -P 16 -q  -h 127.0.0.1 -p 6380 -d 1024 &> s2.out 
#./redis-benchmark -r 1000000 -n 2000000 -c 50 -t get,set -P 16 -q  -h 127.0.0.1 -p 6381 -d 1024 &> s3.out &
#./redis-benchmark -r 1000000 -n 2000000 -c 50 -t get,set -P 16 -q  -h 127.0.0.1 -p 6382 -d 1024 &> s4.out 
