
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
#include <sched.h>

#include "nv_def.h"
#include "nv_map.h"
#ifdef USE_NVMALLOC
	#include "nvmalloc_wrap.h"
#endif
#include "util_func.h"
#include "checkpoint.h"
#include <sys/resource.h>

#define NULL 0

#define USECSPERSEC 1000000
#define pthread_attr_default NULL
#define MAX_THREADS 2

#define BASE_PROC_ID 1000

unsigned int procid;
void * dummy(unsigned);
static unsigned size = 1024 * 1024 * 1;
static unsigned iteration_count = 1;
extern void *je_malloc_(size_t, rqst_s*);

static int grank = 0;

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
    int reg = -1;


#ifdef ENABLE_MPI_RANKS 
	int rank;
	MPI_Comm_rank (MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Bcast(&numprocs, 1, MPI_INT, 0, MPI_COMM_WORLD);
#endif

#ifdef USE_NVMALLOC
    struct rqst_struct rqst;
#endif


   int which = PRIO_PROCESS;
   int pid;
   long cur_mask,new_mask = 1;
   unsigned int len = sizeof(cur_mask);

	     cpu_set_t *cpusetp;
           int num_cpus, cpu;

           num_cpus = 8;

           cpusetp = CPU_ALLOC(num_cpus);
           if (cpusetp == NULL) {
               perror("CPU_ALLOC");
               exit(EXIT_FAILURE);
           }

           size = CPU_ALLOC_SIZE(num_cpus);

           CPU_ZERO_S(size, cpusetp);
           //for (cpu = 0; cpu < num_cpus; cpu += 2)
               CPU_SET_S(7, size, cpusetp);

           printf("CPU_COUNT() of set:    %d\n", CPU_COUNT_S(size, cpusetp));



	pid = getpid();
	setpriority(which, pid, -20);
	if (sched_getaffinity(pid, len,
                     (cpu_set_t *)&cur_mask) < 0) {
	   perror("sched_getaffinity");
	   return NULL;
    }
	printf("pid %d's old affinity: %08lx\n",
           pid, cur_mask);


	if (sched_setaffinity(pid, len, cpusetp)) {
	   perror("sched_setaffinity");
    }

    if (sched_getaffinity(pid, len,
                         (cpu_set_t *)&cur_mask) < 0) {
	   perror("sched_getaffinity");
	   return NULL;
    }

    printf(" pid %d's new affinity: %08lx\n",
           pid, cur_mask);

	

	/*
	 * Time a null loop.  We'll subtract this from the final
	 * malloc loop results to get a more accurate value.
	 */
    while(reg == -1) {
	 reg = reg_for_signal(grank + rank + 1);
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

		rqst.pid = grank + rank + 1 + (j * 3);

try_again:

		fprintf(stdout, "getting data for %d \n",rqst.pid);
		ret = proc_rmt_chkpt(rqst.pid, &bytes, 1);
		if(!ret){
			//fprintf(stdout, "remote chkpt failed \n");
			sleep(4);
			goto try_again;
		}
		//goto skip_remote_send;

		if(numprocs == 1) goto end;

		//goto skip_remote_send;

#ifdef _DEBUG
        fprintf(stderr,"total chkpt to transfer %u \n",bytes);
#endif

		dest_node = (rank + 1) % numprocs;
		if(dest_node == rank)
    	    goto exit;

	    src_node = (rank + numprocs -1)% numprocs;
	    if(src_node == rank)  
			  goto exit;

		MPI_Barrier(MPI_COMM_WORLD);

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
        MPI_Barrier(MPI_COMM_WORLD);

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

	printf("Thread %d elapsed timing: %d.%06d seconds for %d requests"
		" of %d bytes.\n", pthread_self(),
		elapsed.tv_sec, elapsed.tv_usec, total_iterations,
		bytes);
end:
	if(ret) free(ret);
	
	goto start_again;
	//pthread_exit(NULL);
exit:
	return 0;

}

int main(int argc, char *argv[])
{
	unsigned i;
	unsigned thread_count = 1;
	pthread_t thread[MAX_THREADS];

	grank  = atoi(argv[1]);

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


