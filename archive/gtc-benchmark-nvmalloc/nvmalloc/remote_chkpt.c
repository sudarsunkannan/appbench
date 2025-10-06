#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <mpi.h>

#define FILEPATH "/tmp/mmapped3.bin"
#define NUMINTS  (90)
#define FILESIZE (NUMINTS * sizeof(int))
#define __NR_nv_mmap_pgoff     301 
#define __NR_copydirtpages 304
#define MAP_SIZE 1024 * 10
#define SEEK_BYTES 1024 * 10
#define PAGE_SIZE 4096
#define INVALID_INPUT -2;
#define INTERGER_BUFF 1000

 void * realloc_map (void *addr, size_t len, size_t old_size)
  {
          void *p;

          p = mremap (addr, old_size, len, MREMAP_MAYMOVE);
          return p;
  }

struct nvmap_arg_struct{

	unsigned long fd;
	unsigned long offset;
	int chunk_id;
	int proc_id;
	int pflags;
	int noPersist;
	int refcount;
};


int main(int argc, char *argv[])
{
    int i;
    int fd;
    int result;
    char *map, *map2, *map_read;  /* mmapped array of int's */
    void *start_addr;
	int count =0;  
    unsigned long offset = 0;
    int proc_id = atoi(argv[1]);
	int chunk_id =atoi(argv[2]);;
	struct nvmap_arg_struct a;
	int node, numprocs, dest_node, src_node;
    MPI_Status status;
   
	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &node);
	MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
	MPI_Bcast(&numprocs, 1, MPI_INT, 0, MPI_COMM_WORLD);

    i =0;
	a.fd = -1;
	a.offset = offset;
	a.chunk_id =chunk_id;
	a.proc_id =proc_id;
	a.pflags = 1;
	a.noPersist = 0;

	printf("going to mmap readd \n");
	void *dest =   mmap( 0, INTERGER_BUFF * sizeof(unsigned int),  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); //malloc(INTERGER_BUFF * sizeof(unsigned int));
	unsigned int numpages =syscall(__NR_copydirtpages, &a, dest);
	fprintf(stderr, "numpages %ld, bytes %u \n", numpages, numpages*PAGE_SIZE);

	size_t bytes =numpages * PAGE_SIZE;
	char *temp = mmap( 0, bytes,  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(!temp){
		perror("malloc failed \n");
		exit(-1);
	}
	char *destbuff = (char *)malloc(bytes); // (char *)mmap( 0, bytes,  PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	map_read = (char *) syscall(__NR_nv_mmap_pgoff, 0, numpages * PAGE_SIZE,  PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS , &a );
    if (map_read == MAP_FAILED) {
	    close(fd);
    	perror("Error mmapping the file");
	    exit(EXIT_FAILURE);
    }
	 memcpy(temp, map_read, PAGE_SIZE*numpages);
	 
	/*(int j =0;
	for( i = 0; i < numpages; i++){
		//fprintf(stderr,"page dirty:%d \n",i);
		memcpy(temp, map_read, PAGE_SIZE);
		//for ( j = 0; j < 4096; j++)
		//printf(stdout, "%c", (char)temp[j] );
		map_read += PAGE_SIZE;
		temp += PAGE_SIZE;
	}*/
	dest_node = (node + 1) % numprocs;
	if(dest_node == node)
		goto exit;
	
	src_node = (node + numprocs -1)% numprocs;
	if(src_node == node)	goto exit;

	if( node % 2 == 0 ) {
		fprintf(stdout, " %d sending checkpoint data to %d src_node %d\n", node, dest_node, src_node);
		MPI_Buffer_attach(temp,numpages*PAGE_SIZE);
		MPI_Send(temp, bytes, MPI_BYTE, dest_node,0,MPI_COMM_WORLD);
		fprintf(stdout, "after sending %s\n", temp);
		//MPI_Recv(destbuff, numpages*PAGE_SIZE, MPI_BYTE, src_node,0,MPI_COMM_WORLD, &status);
	}else{
		fprintf(stdout, " %d recving checkpoint data from %d dest %d \n", node, src_node, dest_node);
		MPI_Recv(destbuff, numpages*PAGE_SIZE, MPI_BYTE, src_node,0,MPI_COMM_WORLD, &status);
		fprintf(stdout, "after recv %s \n", (char *)destbuff);
		//MPI_Bsend(temp, numpages*PAGE_SIZE, MPI_BYTE, dest_node,1,MPI_COMM_WORLD);
	}
	MPI_Barrier(MPI_COMM_WORLD);
exit:
	MPI_Finalize();
    return 0;
}
