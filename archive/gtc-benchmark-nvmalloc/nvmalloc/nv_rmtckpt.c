
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
#include <sched.h>

#include "nv_def.h"
#include "nv_map.h"
#ifdef USE_NVMALLOC
	#include "nvmalloc_wrap.h"
#endif
#include "util_func.h"
#include "checkpoint.h"
#include <sys/resource.h>

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


void *run_rmt_checkpoint(void *args)
{
	register unsigned int i;
	register unsigned request_size = size;
	register unsigned total_iterations = iteration_count;
	struct timeval start, end, null, elapsed;
	char *ptr = NULL;
    void *ret = 0;
    size_t bytes = 0;
    int src_node =0,dest_node=0;
    void *rcv_buff = 0;
	MPI_Status status;
	int recvsize =0;
    int reg = -1;

	struct arg_struct *thrd_args = (struct arg_struct *)args;
	
	int rank = thrd_args->rank;
	int orig_rank = 0;
	int numprocs = thrd_args->no_procs;


#ifdef USE_NVMALLOC
    struct rqst_struct rqst;
#endif
	/*
	 * Time a null loop.  We'll subtract this from the final
	 * malloc loop results to get a more accurate value.
	 */
	orig_rank = rank -1;
	fprintf(stdout,"trying to register %d\n", thrd_args->rank);

    while(reg == -1) {
	 reg = reg_for_signal(g_rank + rank);
	 sleep(1);
	}

	fprintf(stdout,"registration sucess \n");

start_again:
	gettimeofday(&start, NULL);
	int j =0;
	for (j = 0; j< total_iterations; j++) {

		register void * buf;
		rqst_s rqst;

		rqst.id = j+1;

		rqst.pid = g_rank + rank  + (j * 3);

try_again:

		//fprintf(stdout, "getting data for %d \n",rqst.pid);
		ret = proc_rmt_chkpt(rqst.pid, &bytes, 1);
		if(!ret){
			fprintf(stdout, "remote chkpt failed \n");
			sleep(4);
			goto try_again;
		}
		//goto skip_remote_send;

		if(numprocs == 1) goto end;

		goto skip_remote_send;

#ifdef _DEBUG
        fprintf(stderr,"total chkpt to transfer %u %lu\n",bytes, (unsigned long)ret);
#endif

		dest_node = (orig_rank + 1) % numprocs;
		if(dest_node == orig_rank)
    	    goto exit;

	    src_node = (orig_rank + numprocs -1)% numprocs;
	    if(src_node == orig_rank)  
			  goto exit;

		//MPI_Barrier(MPI_COMM_WORLD);
    	if( orig_rank % 2 == 0 ) {

	        MPI_Send(ret, bytes, MPI_BYTE, dest_node,0,MPI_COMM_WORLD);
	        fprintf(stdout, " %d sending checkpoint"
					"data to %d src_node %d\n", 
					orig_rank, dest_node, src_node);
         
            /*MPI_Probe(src_node, 0, MPI_COMM_WORLD, &status);
  		    MPI_Get_count(&status, MPI_BYTE, &recvsize);
#ifdef _DEBUG
			fprintf(stdout, "Recieved %d bytes \n",recvsize);
#endif
			rcv_buff = malloc(recvsize);
			assert(rcv_buff);

	        MPI_Recv(rcv_buff, recvsize, MPI_BYTE, 
					src_node,0,MPI_COMM_WORLD, &status);*/

	    }else{

			MPI_Probe(src_node, 0, MPI_COMM_WORLD, &status);
  		    MPI_Get_count(&status, MPI_BYTE, &recvsize);
#ifdef _DEBUG
			fprintf(stdout, "Recieved %d bytes \n",recvsize);
#endif
			rcv_buff = malloc(recvsize);
			assert(rcv_buff);
	        MPI_Recv(rcv_buff, recvsize, MPI_BYTE, 
					src_node,0,MPI_COMM_WORLD, &status);
#ifdef _DEBUG
	        fprintf(stdout, " sfter %d recv checkpoint"
					"data to %d \n", 
					orig_rank, dest_node);
#endif 
	        //fprintf(stdout, " %d sending checkpoint"
			//	"data to %d src_node\n",
			//	rank, dest_node);

	        //MPI_Send(ret, bytes, MPI_BYTE, dest_node,0,MPI_COMM_WORLD);
	    }
        //MPI_Barrier(MPI_COMM_WORLD);

		if(rcv_buff && recvsize) {
			//parse_data(rcv_buff, recvsize);
		}
		if(rcv_buff) free(rcv_buff);
	}

skip_remote_send:
	gettimeofday(&end, NULL);
	elapsed.tv_sec = end.tv_sec - start.tv_sec;
	elapsed.tv_usec = end.tv_usec - start.tv_usec;
	if (elapsed.tv_usec < 0) {
		elapsed.tv_sec--;
		elapsed.tv_usec += USECSPERSEC;
	}

	/*printf("Thread %d elapsed timing: %d.%06d seconds for %d requests"
		" of %d bytes.\n", pthread_self(),
		elapsed.tv_sec, elapsed.tv_usec, total_iterations,
		bytes);*/
end:
	if(ret) free(ret);
	
	goto start_again;
	//pthread_exit(NULL);
exit:
	return 0;

}


