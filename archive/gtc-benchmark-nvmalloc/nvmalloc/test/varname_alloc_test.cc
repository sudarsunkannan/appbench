
/*
 *  malloc-test
 *  cel - Thu Jan  7 15:49:16 EST 1999
 *
 *  Benchmark libc's malloc, and check how well it
 *  can handle malloc requests from multiple threads.
 *
 *  Syntax:
 *  malloc-test [ size [ iterations [ thread count ]]]
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include "mpi.h"
#include "jemalloc.h"

#include "nv_def.h"
#include "nv_map.h"
#ifdef USE_NVMALLOC
	#include "nvmalloc_wrap.h"
#endif

#include "util_func.h"

#define NULL 0

#define USECSPERSEC 1000000
#define pthread_attr_default NULL
#define MAX_THREADS 2

#define BASE_PROC_ID 1000

unsigned int procid;
void * dummy(unsigned);
static unsigned size = 1024 * 1024 * 1;
static unsigned iteration_count = 1000; 

extern void *je_malloc_(size_t, rqst_s*);


//#if defined(__cplusplus)
//xtern "C"
//#endif
void *run_test(void* val)
{
	register unsigned int i;
	register unsigned request_size = size;
	register unsigned total_iterations = iteration_count;
	struct timeval start, end, null, elapsed, adjusted;

#ifdef ENABLE_MPI_RANKS 
	int rank;
	MPI_Comm_rank (MPI_COMM_WORLD, &rank);
	//fprintf(stderr,"rank %d \n",rank);
#endif


#ifdef USE_NVMALLOC
    struct rqst_struct rqst;
#endif
	/*
	 * Time a null loop.  We'll subtract this from the final
	 * malloc loop results to get a more accurate value.
	 */
	null.tv_sec = end.tv_sec - start.tv_sec;
	null.tv_usec = end.tv_usec - start.tv_usec;
	if (null.tv_usec < 0) {
		null.tv_sec--;
		null.tv_usec += USECSPERSEC;
	}
	/*
	 * Run the real malloc test
	 */
	gettimeofday(&start, NULL);
	int j =0;
	for (j = 0; j< total_iterations; j++) {

		register void * buf;
		rqst_s rqst;
		rqst.id = j+1;
		rqst.pid = rank+1+ BASE_PROC_ID;
		rqst.var_name = (char *)malloc(10);
		rand_word(rqst.var_name,10); 
		//fprintf(stdout, "var_name %s \n",rqst.var_name);
		je_malloc_(size, &rqst);
		char *ptr = (char *)rqst.nv_ptr;
		assert(ptr);
		for (i = 0; i < size; i++) {
			ptr[i] = 'a';
		}
		//free(buf);
	}

	gettimeofday(&end, NULL);

	elapsed.tv_sec = end.tv_sec - start.tv_sec;
	elapsed.tv_usec = end.tv_usec - start.tv_usec;
	if (elapsed.tv_usec < 0) {
		elapsed.tv_sec--;
		elapsed.tv_usec += USECSPERSEC;
	}

	/*
	 * Adjust elapsed time by null loop time
	 */
	adjusted.tv_sec = elapsed.tv_sec - null.tv_sec;
	adjusted.tv_usec = elapsed.tv_usec - null.tv_usec;
	if (adjusted.tv_usec < 0) {
		adjusted.tv_sec--;
		adjusted.tv_usec += USECSPERSEC;
	}
	printf("Thread %d adjusted timing: %d.%06d seconds for %d requests"
		" of %d bytes.\n", pthread_self(),
		adjusted.tv_sec, adjusted.tv_usec, total_iterations,
		request_size);
	//pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
	unsigned i;
	unsigned thread_count = 1;
	pthread_t thread[MAX_THREADS];

#ifdef ENABLE_MPI_RANKS	
	MPI_Init (&argc, &argv);	
#endif

	run_test(NULL);
#ifdef ENABLE_MPI_RANKS
	MPI_Barrier(MPI_COMM_WORLD);
#endif

	exit(0);
}

void * dummy(unsigned i)
{
	return NULL;
}


