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
#include "c_io.h"
#include <snappy.h>

using namespace snappy;

//int mallocid = 0;
/*void* alloc_( unsigned int size, char *var, int id)
{
	void *buffer = NULL;
	rqst_s rqst;

	rqst.id = ++mallocid;
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
	return nv_chkpt_all(&rqst);

}*/

/*************************************************************/
/*          Example of writing arrays in NVCHPT               */
/*                                                           */
/*************************************************************/
int main (int argc, char ** argv)
{
    char        filename [256],*a;
    int         rank, j;
    int         NY = 100;
    int         *p;
	int 	    *chunk2;
	int mype = 0;
    int base = 0;
	UINT size;
    MPI_Comm    comm = MPI_COMM_WORLD;
    UINT NX, i; 

	NX  = 1024 * 1024 * 50;

    MPI_Init (&argc, &argv);
    MPI_Comm_rank (comm, &rank);

    if(argc > 1)
		base = atoi(argv[1]);

	mype = base + rank;

	size = NX*sizeof(int);
	p = (int *)my_alloc_(&size,(char *)"zion", &mype);
 	chunk2 = (int *)my_alloc_(&size,(char *)"chunk2", &mype);

    for (i = 0; i < NX; i++) {
        p[i] = rank * NX + i;
        //fprintf(stdout, "P[%d]:%d \n",
        //        i, p[i]);
	 }
     for (i = 0; i < NX; i++) {
        chunk2[i] = rank * NX + i;
	 }

	 a = (char *)malloc(size*sizeof(char));
	 for (i = 0; i < size; i++) {
        a[i] = 'a';
	 }

	 nvchkpt_all_(&mype);

 	char *dest = (char *)malloc(size);
	size_t compr_len ;
	snappy::RawCompress((char *)p, NX*sizeof(int),dest,&compr_len);
	fprintf(stdout,"COMPRESS size %u, %u\n", size, compr_len);

    MPI_Barrier (comm);
    MPI_Finalize ();
    return 0;
}





























































