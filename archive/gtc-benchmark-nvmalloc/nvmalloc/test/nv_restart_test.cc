/******************************************************************************
* FILE: mergesort.c
* DESCRIPTION:  
*   The master task distributes an array to the workers in chunks, zero pads for equal load balancing
*   The workers sort and return to the master, which does a final merge
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <math.h>
#include <sched.h>
#include "jemalloc.h"
#include "nv_map.h"
#include "mpi.h"


void* nvread(char *var, int id)
{
	void *buffer = NULL;
	rqst_s rqst;

	rqst.pid = id+1;
	rqst.var_name = (char *)malloc(10);
	memcpy(rqst.var_name,var,10);
	rqst.var_name[10] = 0;
	buffer = nv_map_read(&rqst, NULL);
	buffer = rqst.nv_ptr;
	return buffer;
}



int malloc_cnt = 0;
void* alloc_( unsigned int size, char *var, int id)
{
	void *buffer = NULL;
	rqst_s rqst;

    buffer = nvread(var, id);
    if(buffer)
        return buffer;

	rqst.id = ++malloc_cnt;
	rqst.pid = id+1;
	rqst.var_name = (char *)malloc(10);
	memcpy(rqst.var_name,var,10);
	rqst.var_name[10] = 0;
    //fprintf(stdout, "allocated total %d bytes %s \n", size,rqst.var_name);	
	je_malloc_((size_t)size, &rqst);
	buffer = rqst.dram_ptr;
	return buffer;
}

// allocates n bytes using the 
void* my_alloc_(unsigned int n, char *s, int iid) {

  return alloc_(n, s, iid); 
}
 
void my_free_(char* arr) {
  free(arr);
}
  

int nvchkpt_all_(int mype) {

	rqst_s rqst;

	rqst.pid = mype + 1;
	return nv_chkpt_all(&rqst,1);

}

/*************************************************************/
/*          Example of writing arrays in NVCHPT               */
/*                                                           */
/*************************************************************/
int main (int argc, char ** argv)
{
    char        filename [256];
    int         rank, size, i, j;
    int         NX = 10, NY = 100;
    double      t[NX][NY];
    int         *p;
	int mype = 0;
    MPI_Comm    comm = MPI_COMM_WORLD;

    MPI_Init (&argc, &argv);
    MPI_Comm_rank (comm, &rank);


	mype = atoi(argv[1]);
	p = (int *)nvread((char *)"zion", mype);
    assert(p);
    for (i = 0; i < NX; i++)
        fprintf(stdout, "P[%d]:%d \n",
				i, p[i]);

    MPI_Barrier (comm);
    MPI_Finalize ();
    return 0;
}





























































