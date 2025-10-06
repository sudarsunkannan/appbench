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

/*function to merge vectors*/
double * merge(double *A, int asize, double *B, int bsize) {
  int ai, bi, ci, i;
  double *C;
  int csize = asize+bsize;

  ai = 0;
  bi = 0;
  ci = 0;

  /* printf("asize=%d bsize=%d\n", asize, bsize); */

  C = (double *)malloc(csize*sizeof(double)); /*the array can be statically allocated too*/
  while ((ai < asize) && (bi < bsize)) {
    if (A[ai] <= B[bi]) {
      C[ci] = A[ai];
      ci++; ai++;
    } else {
      C[ci] = B[bi];
      ci++; bi++;
    }
  }

  if (ai >= asize)            /*if A is shorter*/
    for (i = ci; i < csize; i++, bi++)
      C[i] = B[bi];
  else if (bi >= bsize)         /*if B is shorter*/
    for (i = ci; i < csize; i++, ai++)
      C[i] = A[ai];

  for (i = 0; i < asize; i++)
    A[i] = C[i];
  for (i = 0; i < bsize; i++)
    B[i] = C[asize+i];

  /* showVector(C, csize, 0); */
  return C;
}


void swap(double *v, int i, int j)
{
  int t;
  t = v[i];
  v[i] = v[j];
  v[j] = t;
}

void m_sort(double *A, int min, int max)
{
  double *C;    /* dummy, just to fit the function */
  int mid = (min+max)/2;
  int lowerCount = mid - min + 1;
  int upperCount = max - mid;

  /* If the range consists of a single element, it's already sorted */
  if (max == min) {
    return;
  } else {
    /* Otherwise, sort the first half */
    m_sort(A, min, mid);
    /* Now sort the second half */
    m_sort(A, mid+1, max);
    /* Now merge the two halves */
    C = merge(A + min, lowerCount, A + mid + 1, upperCount);
  }
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


int sort_func_( float *f, int *temp, int *num_proc, int *iid)
{
	int cnt;

	fprintf(stderr,"calling sort_func \n");
	
#ifdef FREQ_MEASURE
	 //For measurement;
  double local_frequency = 0;
  if(!g_iofreq_time) {
		g_iofreq_time = MPI_Wtime();
    local_frequency = MPI_Wtime();
  }else{
			local_frequency = MPI_Wtime();
	    fprintf(stdout, "IO frequency %f \n",local_frequency-	g_iofreq_time);
	}
#endif


#ifdef _NONVMEM
   /*for (cnt=0; cnt < *temp; cnt++) {
			printf("PRINTING  %f \n",f[cnt]);
   }
   fprintf(stderr,"SIZE %lu \n",*temp);*/
    return 0;
#endif

	double * data;
	double * chunk = NULL;
	double * other= NULL;
	int m,n=0;
	int id,p;
	int s = 0;
	int i;
	int step;
	MPI_Status status;

  p = *num_proc;
  id =  *iid;
  n =  *temp;
	startT = MPI_Wtime();
 
  //nv changes
  long idx = 0;
  struct rqst_struct *rqst = NULL;
  double *buffer = NULL;
  double start_nv, stopT;

  initialize_nv(1);

#ifdef _NVMEM

      if(val == 0) {
				start_nv = MPI_Wtime();
       //fprintf(stderr,"start: %f \n", start_nv);
      } 
 
	    if( n > 0 && val % FREQUENCY == 0){
        	 idx = n;
	         rqst = (struct rqst_struct *)malloc(sizeof(struct rqst_struct));
    	     rqst->id = val+1;
        	 rqst->bytes =  idx*sizeof(float)*4;
           total_bytes += rqst->bytes; 
           if(*iid == 0)
           fprintf(stderr,"%d \n", total_bytes); 
	         rqst->pid = *iid;
    	     nvmalloc(rqst);
        	 buffer = (float *)malloc(sizeof(float) * rqst->bytes);
	         memcpy( buffer, f, (sizeof(float) * rqst->bytes));
    	     rqst->src = (unsigned long)buffer;
        	 rqst->order_id = idx;
	         nv_commit(rqst);
       }
       if(buffer)
          free(buffer);

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

#ifdef LOCAL_PROCESSING

   int pid = fork();

  	if( n > 0 && val% FREQUENCY == 0) {
	  		 int r;
			   s = n;
			   chunk = (double *)malloc(s*sizeof(double));
				 for(i=0;i<n;i++){
			      chunk[i]=f[i];
				  }
				  m_sort(chunk, 0, s-1);  
	
					/*data propagation in a tree fashion*/
					step = 1;
					while(step<p)
					{
						if(id%(2*step)==0)
						{
							if(id+step<p)
							{
								MPI_Recv(&m,1,MPI_DOUBLE,id+step,0,MPI_COMM_WORLD,&status);
								other = (double *)malloc(m*sizeof(double));
								MPI_Recv(other,m,MPI_DOUBLE,id+step,0,MPI_COMM_WORLD,&status);
								chunk = merge(chunk,s,other,m);
								s = s+m;
							} 
						}
						else
						{
							int near = id-step;
							MPI_Send(&s,1,MPI_DOUBLE,near,0,MPI_COMM_WORLD);
							MPI_Send(chunk,s,MPI_DOUBLE,near,0,MPI_COMM_WORLD);
							break;
						}
						step = step*2;
				}

  			stopT = MPI_Wtime();
	  		 printf("%d; %d processors; %f secs\n", n, p, (stopT-startT));
				/*if(id==0)
				{
					FILE * fout;
					fout = fopen("result","w");
					for(i=0;i<s;i++)
						fprintf(fout,"%f\n",chunk[i]);
						fclose(fout);
				}*/
		 } 

#endif

 val++;

 return 0;
}
