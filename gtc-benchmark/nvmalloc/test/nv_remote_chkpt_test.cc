
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
#include "checkpoint.h"

#define NULL 0

#define USECSPERSEC 1000000
#define pthread_attr_default NULL
#define MAX_THREADS 2

#define BASE_PROC_ID 1000

#define _DEBUG

unsigned int procid;
void * dummy(unsigned);
static unsigned size = 1024 * 1024 * 1;
static unsigned iteration_count = 1;
extern void *je_malloc_(size_t, rqst_s*);

static int g_rank = 0;

//#if defined(__cplusplus)
//xtern "C"
//#endif
void *run_test(void* val)
{
	register unsigned int i;
	register unsigned request_size = size;
	register unsigned total_iterations = iteration_count;
	struct timeval start, end, null, elapsed;
	char *ptr = NULL;
    void *ret = 0;
    size_t bytes = 0;
    int numprocs=0, src_node =0,dest_node=0;
    void *rcv_buff = 0;
	MPI_Status status;
	int recvsize =0;


#ifdef ENABLE_MPI_RANKS 
	int rank;
	MPI_Comm_rank (MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Bcast(&numprocs, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif

#ifdef USE_NVMALLOC
    struct rqst_struct rqst;
#endif
	/*
	 * Time a null loop.  We'll subtract this from the final
	 * malloc loop results to get a more accurate value.
	 */
	gettimeofday(&start, NULL);
	int j =0;
	for (j = 0; j< total_iterations; j++) {

		register void * buf;
		rqst_s rqst;

		rqst.id = j+1;

		rqst.pid = g_rank + rank + 1;

		ret = proc_rmt_chkpt(rqst.pid, &bytes, 1);
		assert(ret);

#ifdef _DEBUG
        fprintf(stderr,"total chkpt to transfer %u \n",bytes);
#endif

		dest_node = (rank + 1) % numprocs;
		if(dest_node == rank)
    	    goto exit;

	    src_node = (rank + numprocs -1)% numprocs;
	    if(src_node == rank)  
			  goto exit;

    	if( rank % 2 == 0 ) {


	        MPI_Send(ret, bytes, MPI_BYTE, dest_node,0,MPI_COMM_WORLD);

	       // fprintf(stdout, " %d sending checkpoint"
			//		"data to %d src_node %d\n", 
			//		rank, dest_node, src_node);
         
            MPI_Probe(src_node, 0, MPI_COMM_WORLD, &status);
  		    MPI_Get_count(&status, MPI_BYTE, &recvsize);
#ifdef _DEBUG
			fprintf(stdout, "Recieved %d bytes \n",recvsize);
#endif
			rcv_buff = malloc(recvsize);

	        MPI_Recv(rcv_buff, recvsize, MPI_BYTE, 
					src_node,0,MPI_COMM_WORLD, &status);

	    }else{

			MPI_Probe(src_node, 0, MPI_COMM_WORLD, &status);
  		    MPI_Get_count(&status, MPI_BYTE, &recvsize);
#ifdef _DEBUG
			fprintf(stdout, "Recieved %d bytes \n",recvsize);
#endif
			rcv_buff = malloc(recvsize);
	        MPI_Recv(rcv_buff, recvsize, MPI_BYTE, 
					src_node,0,MPI_COMM_WORLD, &status);

#ifdef _DEBUG
	        fprintf(stdout, " sfter %d recv checkpoint"
					"data to %d \n", 
					rank, dest_node);
#endif

	        //fprintf(stdout, " %d sending checkpoint"
			//	"data to %d src_node\n",
			//	rank, dest_node);

	        MPI_Send(ret, bytes, MPI_BYTE, dest_node,0,MPI_COMM_WORLD);
	    }
	}

	if(rcv_buff && recvsize) {

		//parse_data(rcv_buff, recvsize);
	}

	gettimeofday(&end, NULL);
	elapsed.tv_sec = end.tv_sec - start.tv_sec;
	elapsed.tv_usec = end.tv_usec - start.tv_usec;
	if (elapsed.tv_usec < 0) {
		elapsed.tv_sec--;
		elapsed.tv_usec += USECSPERSEC;
	}

	printf("Thread %d elapsed timing: %d.%06d seconds for %d requests"
		" of %d bytes.\n", pthread_self(),
		elapsed.tv_sec, elapsed.tv_usec, total_iterations,
		bytes);
	//pthread_exit(NULL);
exit:
	return 0;

}

int main(int argc, char *argv[])
{
	unsigned i;
	unsigned thread_count = 1;
	pthread_t thread[MAX_THREADS];

	g_rank  = atoi(argv[1]);

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


