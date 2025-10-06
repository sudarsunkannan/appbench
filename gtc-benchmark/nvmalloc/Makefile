INCLUDE=$(NVMALLOC_HOME)
src_path=$(NVMALLOC_HOME)
LIB_PATH=$(NVMALLOC_HOME)

#CXX=/usr/lib/mvapich2/bin/mpicxx
#CC=/usr/lib/mvapich2/bin/mpicc

CXX=/usr/bin/mpicxx
CC=/usr/bin/mpicc
GNUFLAG :=  -std=gnu99
CPPFLAGS := -std=gnu++0x
CFLAGS := -g -I$(INCLUDE) -Wall -pipe -fvisibility=hidden -funroll-loops  -Wno-implicit -Wno-uninitialized \
		  -Wno-unused-function  -lm -lssl  -lcrypto
# See source code comments to avoid memory leaks when enabling MALLOC_MAG.
#CPPFLAGS := -DMALLOC_PRODUCTION -DMALLOC_MAG
CPPFLAGS := -DMALLOC_PRODUCTION 
#LIBS= -ljemalloc -lpthread
LIBS= -lpthread -lssl  -lcrypto

all:  nvmalloc_bench

JEMALLOC_OBJS= 	$(src_path)/jemalloc.o $(src_path)/arena.o $(src_path)/atomic.o \
				$(src_path)/base.o $(src_path)/ckh.o $(src_path)/ctl.o $(src_path)/extent.o \
			    $(src_path)/hash.o $(src_path)/huge.o $(src_path)/mb.o \
			    $(src_path)/mutex.o $(src_path)/prof.o $(src_path)/quarantine.o \
			    $(src_path)/rtree.o $(src_path)/stats.o $(src_path)/tcache.o \
			    $(src_path)/util.o $(src_path)/tsd.o $(src_path)/chunk.o \
				$(src_path)/bitmap.o $(src_path)/chunk_mmap.o $(src_path)/chunk_dss.o \
				$(src_path)/rbtree.o $(src_path)/nv_map.o  $(src_path)/checkpoint.o \
				$(src_path)/util_func.o $(src_path)/gtthread_spinlocks.o $(src_path)/nv_rmtckpt.o \
								

$(src_path)/nv_map.o: $(src_path)/nv_map.cc 
	$(CXX) -c $(src_path)/nv_map.cc -o $(src_path)/nv_map.o $(LIBS) $(src_path)/rbtree.o

$(src_path)/rbtree.o: $(src_path)/rbtree.cc
	$(CXX) -c $(src_path)/rbtree.cc -o $(src_path)/rbtree.o $(LIBS)

$(src_path)/checkpoint.o: $(src_path)/checkpoint.cc
	$(CXX) -c $(src_path)/checkpoint.cc -o  $(src_path)/checkpoint.o  $(LIBS) $(CPPFLAGS)

$(src_path)/gtthread_spinlocks.o: $(src_path)/gtthread_spinlocks.cc
	$(CXX) -c $(src_path)/gtthread_spinlocks.cc -o  $(src_path)/gtthread_spinlocks.o  $(LIBS) $(CPPFLAGS)

#$(src_path)/zone.o

nv_read_test: $(JEMALLOC_OBJS) nv_read_test.cc
	$(CXX) nv_read_test.cc -o nv_read_test $(JEMALLOC_OBJS) $(CFLAGS) $(LIBS) 

nvmalloc_bench:$(JEMALLOC_OBJS) nvmalloc_bench.cc
	$(CXX) nvmalloc_bench.cc -o nvmalloc_bench $(JEMALLOC_OBJS) $(CFLAGS) $(LIBS) -I$(INCLUDE)/jemalloc

test_dirtypgcpy:$(JEMALLOC_OBJS) test_dirtypgcpy.c
	$(CXX) test_dirtypgcpy.c -o test_dirtypgcpy $(JEMALLOC_OBJS) $(CFLAGS) $(LIBS)


clean:
	rm -f *.o *.so.0 *.so nv_read_test
	rm -f nvmalloc_bench
	rm -f test_dirtypgcnt test_dirtypgcpy
	rm -f ../*.o
