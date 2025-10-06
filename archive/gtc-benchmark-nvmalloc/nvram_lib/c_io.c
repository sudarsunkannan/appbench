/******************************************************************************
* FILE: mergesort.c
* DESCRIPTION:  
*   The master task distributes an array to the workers in chunks, zero pads for equal load balancing
*   The workers sort and return to the master, which does a final merge
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>
#include <fcntl.h>

#define N 100000
#define MASTER 0		/* taskid of first task */

#include "oswego_malloc.h"
#include "nv_map.h"
#include "nv_def.h"
#include <math.h>
#include <sched.h>
//#include "client.h"

//#define _NONVMEM
#define _NVMEM
//#define IO_FORWARDING
//#define LOCAL_PROCESSING
#define FREQUENCY 1
//#define FREQ_MEASURE

void showdata(double *v, int n, int id);
double * merge(double *A, int asize, double *B, int bsize);
void swap(double *v, int i, int j);
void m_sort(double *A, int min, int max);

double startT, stopT;
double startTime;
unsigned long total_bytes=0;

#ifdef FREQ_MEASURE
//For measurement
double g_iofreq_time=0;
#endif



#ifdef IO_FORWARDING
double io_fwd_start_time=0;
double io_fwd_end_time=0;	
double total_forward_time=0;
#endif


/*function to print a vector*/
void showdata(double *v, int n, int id)
{
  int i;
  //printf("%d: ",id);
  for(i=0;i<n;i++);
    //printf("%d: ID %f \n",v[i], id);
  //putchar('\n');
}


int val =0;
double start_time=0;
double end_time = 0;

int start_(int *mype) {

  if(*mype == 0){
	  start_time=MPI_Wtime();
		fprintf(stdout,"START TIME: %f \n", (start_time));
  }
  return 0;
}


int end_(int *mype, float *itr) {

  int fd = -1;

  if(*mype == 0) {
	  end_time=MPI_Wtime();
	  //fprintf(stdout,"END TIME: %f wallclock %f io_frwd_time %f \n ", (end_time - start_time), *itr, total_forward_time);
    fprintf(stdout,"END TIME: %f wallclock %f io_frwd_time %f \n ", (end_time - start_time), *itr);
	  fd = open("status.txt", O_WRONLY | O_CREAT | O_TRUNC, 	0666);
		if(fd == -1){
			perror("error opening file");
			return -1;	
		} 
	  write(fd, (char *)"done",4);
	  close(fd);
  }

  return 0; 
}  


int write_io_( float *f, int *elements, int *num_proc, int *iid)
{
	int cnt;
	double * data;
	double * chunk = NULL;
	double * other= NULL;
	int m,n=0;
	int id,p;
	int s = 0;
	int i;
	int step;
	MPI_Status status;

	  //nv changes
	long idx = 0;
	struct rqst_struct rqst;
	double *buffer = NULL;
	double start_nv, stopT;

	fprintf(stderr,"calling sort_func \n");

#ifdef _NONVMEM
   /*for (cnt=0; cnt < *temp; cnt++) {
			printf("PRINTING  %f \n",f[cnt]);
   }
   fprintf(stderr,"SIZE %lu \n",*temp);*/
    return 0;
#endif

	p = *num_proc;
	id =  *iid;
	n =  *elements;
	startT = MPI_Wtime();
	//initialize_nv(1);

#ifdef _NVMEM

      if(val == 0) {
		start_nv = MPI_Wtime();
      } 
 
	    if( n > 0){
        	 idx = n;
    	     rqst.id = val+1;
        	 rqst.bytes =  idx*sizeof(float)*4;
           	 total_bytes += rqst.bytes; 

             if(*iid == 0){	
 	         	 fprintf(stderr,"%lu \n", total_bytes); 
			 }
				
	         rqst.pid = *iid;
			 //buffer =malloc((size_t)total_bytes);
			 fprintf(stdout, "total bytes %d \n", total_bytes);
    	     buffer = pnv_malloc((size_t)total_bytes, &rqst);
	         memcpy( buffer, f, (sizeof(float) * total_bytes));
    	     //rqst.src = (unsigned long)buffer;
        	 //rqst.order_id = idx;
	         //nv_commit(rqst);
       }
       //if(buffer)
          //free(buffer);

#endif

#ifdef IO_FORWARDING
  s = n;
 if( n > 0 && val% FREQUENCY == 0) {
	
	if(id == 0){
	  io_fwd_start_time= MPI_Wtime();
  }
    float *buf =  malloc(s*4*sizeof(float));
  	forward_data(buf, *iid, s*4*sizeof(float),p);
  if(id == 0){
	  io_fwd_end_time = MPI_Wtime();
  	total_forward_time += io_fwd_end_time - io_fwd_start_time;
  }
  if(buf)
	free(buf);
  
 }
#endif

 val++;

 return 0;
}
