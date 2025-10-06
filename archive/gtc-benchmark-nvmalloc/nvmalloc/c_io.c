/******************************************************************************
* FILE: mergesort.c
* DESCRIPTION:  
*   The master task distributes an array to the workers in chunks, zero pads for equal load balancing
*   The workers sort and return to the master, which does a final merge
******************************************************************************/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

//#include <mpi.h>
#include <fcntl.h>

#define N 100000
#define MASTER 0		/* taskid of first task */

#include "util_func.h"
#include <sys/time.h>
#include <math.h>
#include <sched.h>
#include "jemalloc/jemalloc.h"
#include "nv_map.h"
#include <pthread.h>
#include "nv_rmtckpt.h"
#include <signal.h>
#include <sys/queue.h>
#include <sys/resource.h>


//#define _NONVMEM
#define _NVMEM
//#define IO_FORWARDING
//#define LOCAL_PROCESSING
#define FREQUENCY 1
//#define FREQ_MEASURE
//
//
#define THRES_ASYNC 16000000

void showdata(double *v, int n, int id);
double * merge(double *A, int asize, double *B, int bsize);
void swap(double *v, int i, int j);
void m_sort(double *A, int min, int max);
extern long simulation_time(struct timeval start,
			 struct timeval end );

int thread_init = 0;
double startT, stopT;
double startTime;
unsigned long total_bytes=0;
int iter_count =0;

struct timeval g_start, g_end;
struct timeval g_chkpt_inter_strt, g_chkpt_inter_end;
int g_mypid=0;
#ifdef FREQ_MEASURE
//For measurement
double g_iofreq_time=0;
#endif

pthread_mutex_t precommit_mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t precommit_cond = PTHREAD_COND_INITIALIZER;
int precommit=0;
int curr_chunkid =0;
int prev_chunkid =0;


#define handle_error(msg) \
           do { perror(msg); exit(EXIT_FAILURE); } while (0)







static void
handler(int sig, siginfo_t *si, void *unused)
{

	size_t length = 0;

	//if(curr_chunkid ==0)
  	//TAILQ_INIT(&head);

    //fprintf(stdout,"recvd seg fault \n");
    length = nv_disablprot(si->si_addr, &curr_chunkid);
	assert(length > 0);

	if(prev_chunkid == 0) {
         prev_chunkid = curr_chunkid;
    }else {
		//add_to_queue(prev_chunkid);
		add_to_fault_lst(prev_chunkid);
		//pthread_mutex_unlock(&precommit_mtx);
    	prev_chunkid = curr_chunkid;
		precommit = 1;
		pthread_cond_signal(&precommit_cond);

	  	/*gettimeofday(&g_chkpt_inter_end, NULL);
		long simtime = simulation_time(g_chkpt_inter_strt, g_chkpt_inter_end);
        if(simtime > 5000000){
			//fprintf(stdout,"simtime %ld \n", simtime);
			precommit = 1;
			pthread_cond_signal(&precommit_cond);
		}*/
    }
}



int assing_aff() {

	int core_id = 11;

   int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
   if (core_id >= num_cores)
      return;

   cpu_set_t cpuset;
   CPU_ZERO(&cpuset);
   CPU_SET(core_id, &cpuset);

   pthread_t current_thread = pthread_self();    
   int return_val = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);

}





void* set_protection(void *arg)
{

	//if(set_chunkprot()) {
	//	fprintf(stdout, "chunk protection failed \n");
	//}

   //assing_aff();
   //
	while(1) {

		int target_chunk =0;
		pthread_mutex_lock(&precommit_mtx);
		while(!precommit)
		pthread_cond_wait(&precommit_cond, &precommit_mtx);


	  	gettimeofday(&g_chkpt_inter_end, NULL);
		long simtime = simulation_time(g_chkpt_inter_strt, g_chkpt_inter_end);
        if( simtime < THRES_ASYNC){

			long sleep_time = THRES_ASYNC - simtime;
			sleep_time = sleep_time/1000000;
			if(g_mypid == 1)
			fprintf(stdout,"going to sleep for %ld \n",sleep_time);
			sleep(sleep_time);

		}

		/*if(prev_chunkid == 0) {
			 prev_chunkid = curr_chunkid;
			 precommit=0;
			 pthread_mutex_unlock(&precommit_mtx);			
			 continue;
		}else {

			target_chunk = prev_chunkid;
			prev_chunkid = curr_chunkid;
		}*/
			/*struct entry *item;
			TAILQ_FOREACH(item, &head, entries) {
				if(item->copied){
					//pthread_mutex_lock(&precommit_mtx);
				 	TAILQ_REMOVE(&head, item, entries);
					//pthread_mutex_unlock(&precommit_mtx);
					free(item);
					continue;
				}
				target_chunk = item->id;
				item->copied = 1;
				//free(item)	
				//if(g_mypid == 1)
				fprintf(stdout, "recieved signal, going to copy \n");
				if(start_asyn_lcl_chkpt(target_chunk)) {
				fprintf(stdout, "chunk protection failed \n");
				}
			}*/
			//fprintf(stdout, "recieved signal, going to copy \n");
			start_asyn_lcl_chkpt(target_chunk);
			//sleep(8);
		        precommit=0;
			pthread_mutex_unlock(&precommit_mtx);
			//sleep(2);
	}
	return 0;
}


int start_precommit_() {

	//precommit=1;
	//pthread_cond_signal(&precommit_cond);

}

void start_async_commit()
{
    pthread_t thread1;
    int  iret1;
   	struct sigaction sa;
	struct sched_param param;
	pthread_attr_t lp_attrNULL;
	int s =0;	
	int policy, min_priority;

    sa.sa_flags = SA_SIGINFO;
   	sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = handler;
    if (sigaction(SIGSEGV, &sa, NULL) == -1)
   	    handle_error("sigaction");

/*	pthread_attr_init(&lp_attr);
	pthread_attr_setinheritsched(&lp_attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&lp_attr, SCHED_FIFO);
	min_priority = sched_get_priority_min(SCHED_FIFO);
	param.sched_priority = min_priority;
	pthread_attr_setschedparam(&lp_attr, &param);
*/
    /* Create independent threads each of which will execute function */
     iret1 = pthread_create(&thread1, NULL, set_protection, (void*)NULL);
}

#ifdef REMOTE_CHECKPOINT
void start_rmt_checkpoint(int numprocs, int rank)
{
     pthread_t thread1;
     int  iret1;
	 struct arg_struct *s;

	s = (struct arg_struct *) malloc(sizeof(struct arg_struct));
	s->rank = rank;
	s->no_procs = numprocs;

    /* Create independent threads each of which will execute function */
     iret1 = pthread_create( &thread1, NULL, run_rmt_checkpoint, (void*)s);
}
#endif

/* To calculate simulation time */
long simulation_time(struct timeval start, struct timeval end )
{
	long current_time;

	current_time = ((end.tv_sec * 1000000 + end.tv_usec) -
                	(start.tv_sec*1000000 + start.tv_usec));

	return current_time;
}

int starttime_(int *mype) {

  //if(*mype == 0)
  {
      gettimeofday(&g_start, NULL);	
  }
  return 0;
}


int endtime_(int *mype, float *itr) {

  //if(*mype == 0)
   {
	gettimeofday(&g_end, NULL);
   	//fprintf(stderr,"END TIME: %ld mype %d \n ",
	//		 simulation_time(g_start, g_end),(int)*mype);
  }
  return 0; 
}

void* nvread(char *var, int id)
{
    void *buffer = NULL;
    rqst_s rqst;

    rqst.pid = id+1;
    rqst.var_name = (char *)malloc(10);
    memcpy(rqst.var_name,var,10);
    rqst.var_name[10] = 0;
	//fprintf(stdout,"proc %d var %s\n",id, rqst.var_name);
    buffer = nv_map_read(&rqst, NULL);
    buffer = rqst.dram_ptr;
    if(rqst.var_name)
        free(rqst.var_name);
    return buffer;
}


/*--------------------------------------*/
//testing code. delete after analysis
//
#ifdef _FAULT_STATS
int num_faults =0;
void *g_tmpbuff;
size_t g_tmpbuff_sz;
size_t total_pages;

static void
temp_handler(int sig, siginfo_t *si, void *unused)
{

   size_t len = 4096;
   void *addr = (unsigned long)si->si_addr & ((0UL - 1UL) ^ (4096 - 1));
   int diff =   (unsigned long)si->si_addr - (unsigned long)addr;

   //fprintf(stdout,"diff %d (unsigned long)addr %lu, (unsigned long)si->si_addr %lu\n",diff, addr, si->si_addr);
   fprintf(stdout,"num_faults %d total_pages %d \n",++num_faults, total_pages);
    if (mprotect(addr, 4096, PROT_READ|PROT_WRITE)==-1) {
        fprintf(stdout,"%lu\n", si->si_addr);
        perror("mprotect");
        exit(-1);
    }

}


int register_handler() {

    struct sigaction sa;
    struct sched_param param;

    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = temp_handler;
    if (sigaction(SIGSEGV, &sa, NULL) == -1)
        handle_error("sigaction");

}

int temp_protection(void *addr, size_t len, int flag){


    if (mprotect(addr,len, PROT_READ)==-1) {
        fprintf(stdout,"%lu \n", (unsigned long)addr);
        perror("mprotect: temp_prot");
      	exit(-1); 
    }
    
        return 0;
}

#endif //#ifdef _FAULT_STATS
/*-------------------------------------------------------*/



int mallocid = 0;
void* alloc_( unsigned int size, char *var, int id, int commit_size)
{
	void *buffer = NULL;
	rqst_s rqst;

	init_checkpoint(id+1);

#ifdef ENABLE_RESTART
	buffer = nvread(var, id);
	if(buffer) {
		//fprintf(stdout, "nvread succedded \n");
		//return malloc(size);
		return buffer;
     }
#endif

	g_mypid = id+1;
	rqst.id = ++mallocid;
	rqst.pid = id+1;
	rqst.commitsz = commit_size;
	rqst.var_name = (char *)malloc(10);
	memcpy(rqst.var_name,var,10);
	rqst.var_name[10] = 0;
	je_malloc_((size_t)size, &rqst);
	buffer = rqst.dram_ptr;
	assert(buffer);
	if(rqst.var_name)
	  free(rqst.var_name);

    //fprintf(stdout, "allocated total %d bytes %s \n", size,var);	
	//fprintf(stdout,"proc %d leaving alloc_\n",id);

#ifdef _FAULT_STATS
	if(g_mypid == 1) {

		register_handler();
		total_pages = total_pages + (size/4096);	
		//if(strstr(var,"zion")) {
		//	g_tmpbuff_sz = size;
		//	g_tmpbuff = buffer;	
	  temp_protection(buffer,commit_size, PROT_READ);
	}
#endif

	return buffer;
}

// allocates n bytes using the 
void* my_alloc_(unsigned int* n, char *s, int *iid, int *cmtsize) {

  return alloc_(*n, s, *iid, *cmtsize); 
}





int fd = -1;

void write_io_(char* fname, float *buff, int *size, int *restart) {

	
	if(fd == -1 || *restart) 	
		fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC,0777);

  //return alloc_(*n, s, *iid); 
	int sz =0;
	sz = write(fd,buff, *size*4);
	lseek(fd, *size, SEEK_SET);
	//fprintf(stdout ,"SIZE %d written %u name %s , fd %d\n",*size, sz, fname, fd);
}
 

 
void my_free_(char* arr) {
  free(arr);
}
  

int nvchkpt_all_(int *mype) {

	rqst_s rqst;
	int ret =0;

	if(g_mypid == 1)
	fprintf(stdout,"TAKING checkpoint \n");

	//gettimeofday(&g_chkpt_inter_end, NULL);
	//fprintf(stdout,"CHECKPOINT TIME: %ld \n ",
    //                   simulation_time(g_chkpt_inter_strt, g_chkpt_inter_end));
	//gettimeofday(&g_chkpt_inter_strt, NULL);
     //
	rqst.pid = *mype + 1;
	if(iter_count % 10000 == 0)
		ret= nv_chkpt_all(&rqst, 1);
	else
		ret= nv_chkpt_all(&rqst, 0);


	if(!thread_init) {
		//start_rmt_checkpoint(12,id+1);
#ifdef _ASYNC_LCL_CHK
		start_async_commit();
#endif
		//set_chunkprot();
		thread_init = 1;
	}

	gettimeofday(&g_chkpt_inter_strt, NULL);
	iter_count++;

#ifdef _FAULT_STATS
	num_faults = 0;
#endif

	//fprintf(stdout,"proc %d entering chkpt\n",*mype);
	return ret;
}


void *nv_restart_(char *var, int *id) {
	
	return nvread(var, *id);
}















