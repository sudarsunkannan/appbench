
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
#include "nv_def.h"
#include "nv_map.h"

#ifdef USE_NVMALLOC
	#include "nvmalloc_wrap.h"
#endif

#define USECSPERSEC 1000000
#define pthread_attr_default NULL
#define MAX_THREADS 2

#define BASE_PROC_ID 10000

unsigned int procid;
void run_test(void);
static unsigned size = 1024 * 1024 * 1;
static unsigned iteration_count = 1; 

int main(int argc, char *argv[])
{
	unsigned i;
	unsigned thread_count = 1;
	pthread_t thread[MAX_THREADS];

#ifdef ENABLE_MPI_RANKS	
	MPI_Init (&argc, &argv);	
#endif

	printf("Starting test...\n");
	run_test();

	exit(0);
}

void run_test(void)
{
	register unsigned int i;
	register unsigned request_size = size;
	register unsigned total_iterations = iteration_count;
	struct timeval start, end, null, elapsed, adjusted;

	int rank;
	MPI_Comm_rank (MPI_COMM_WORLD, &rank);
	fprintf(stderr,"rank %d \n",rank);
	procid = rank + 1;

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
	for (i = 0; i < total_iterations; i++) {
		register void * buf;

		rqst_s rqst;
		rqst.id = i+1;
		rqst.var_name = NULL;
		rqst.pid = rank+1+ BASE_PROC_ID;
		fprintf(stdout, "requesting chunk %d \n",rqst.id);	
		buf = nv_map_read(&rqst, NULL);
		assert(buf);
	    fprintf(stdout,"buf %s \n", buf);	
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

	pthread_exit(NULL);
}


