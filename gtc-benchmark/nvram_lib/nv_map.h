/*
 * nv_map.h
 *
 *  Created on: Mar 20, 2011
 *      Author: sudarsun
 */

#ifndef NV_MAP_H_
#define NV_MAP_H_


#include "list.h"
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>

#include "red_black_tree.h"

//#include "gtthread_spinlocks.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long ULONG;
typedef unsigned int  UINT;

enum CHUNKFLGS { PROCESSED =1};


/*Every malloc call will lead to a chunk creation*/
struct chunk {

	unsigned int mmap_id;
	unsigned long mmap_straddr;

    unsigned int vma_id;
    unsigned long length;
    unsigned long offset;
    struct proc_obj *proc_obj;
    struct list_head next_chunk;
    //chunk processing information
    int proc_id;
#ifdef CHCKPT_HPC
    //field indicator to order chunks
    //unsigned long order_id;
    int ops;
    //chunk flags
    //FIXME as of now just used for processing
    enum CHUNKFLGS flags; 
#endif

};



/* Each user process will have a process obj
 what about threads??? */
struct proc_obj {
    int pid;
    struct list_head next_proc;
    struct list_head chunk_list;

    rb_red_blk_tree* chunk_tree;

    unsigned int chunk_initialized;

    /*process chunk start address*/
    unsigned long curr_heap_addr;

    /*starting virtual address of process*/
    unsigned long start_addr;

    /*size*/
    unsigned long size;

   /*current offset. indicates where is the offset now 
    pointing to */
   unsigned long offset;

   unsigned long data_map_size;

   int num_chunks;

   unsigned long meta_offset;

   int file_desc;

   //indicates total number of large blocks
   int num_mmaps;

};



struct rqst_struct {
    size_t bytes;
    const char* var;
    //unique id on how application wants to identify 
    //this chunk;
    int id;
    int pid;
    
    int ops;
    void *src;
    unsigned long mem;
    unsigned int order_id;

    //volatile flag
    int isVolatile;

    unsigned int mmap_id;
    unsigned long mmap_straddr;

   
};

struct nvmap_arg_struct{
    unsigned long fd;
	unsigned long offset;
    int chunk_id;
    int proc_id;
	int pflags;
	int ref_count;
	int noPersist;
};

struct queue {

    unsigned long offset;
    unsigned int num_chunks;      

    /*This lock decides the parallel 
     nv ram access */
    //struct gt_spinlock_t lock;
  
    /*Lock used by active memory processing*/  
    int outofcore_lock; 

    struct list_head lchunk_list;
    int list_initialized;
};

static struct queue *l_queue;
//Function to read nv queue
struct queue *get_queue(void);
//Function to get next chunk from queue
struct chunk* get_nxt_chunk(struct queue*,struct chunk*);
//Function to get the memory location of the chunk
void *get_chunk_mem( struct chunk *chunk, int i);
//gets the memory location from queue that needs to be processed
void *get_next_queue_item( struct rqst_struct *rqst, int index,int rank );
//Gets the total queue chunks currently present in queue
unsigned int get_num_queue_chunks(int timestep);
//This function increments outof core processing
//to enable outof core processing
int incr_outof_core_lock(void);
//This function decrements outof core processing
//to disable outof core processing
int decr_outof_core_lock(void);
//This function enable out of core processing
//by enabling the chunk queue lock
int is_outof_core_lock_disabled(void);
//function to print the out of core lock
//value
int print_outof_core_lock(void);


//Function to check if chunk queue is already initialized
//FIXME: if multiple queues existis, then it should have a
// identifier
void* check_if_init();


void* nv_mmap(struct rqst_struct *);

void* nv_map_read(struct rqst_struct *, void *);

int nv_commit(struct rqst_struct *);

int nv_munmap(void *addr);

unsigned int generate_vmaid(const char *key);

ULONG findoffset(UINT proc_id, ULONG curr_addr);

//int update_offset(UINT proc_id, ULONG offset, struct rqst_struct *rqst);
int update_offset(UINT proc_id, unsigned int offset, struct rqst_struct *rqst);

//Should be first called
int initialize_nv(int sema);

/*Queue related changes */

//first time initialization
static int intialized;

//semaphore intialization
static  int initialize_sema;

void *create_queue();

/*intialize library*/
int intialize(int pid);

/*check if queue exists */
void* check_if_init();

/*Debugging functions */
void print_chunk(struct chunk *chunk);

/*get process start address */
unsigned long  get_proc_strtaddress(struct rqst_struct *);

/*function to get process mmmap num*/
int get_proc_num_maps(int pid);

/*function to initialize process nv structure*/
int nv_initialize(struct rqst_struct *rqst);


static inline void PRINT(const char* format, ... ) {

    va_list args;
    va_start( args, format );
    fprintf( stderr, format, args );
    va_end( args );
}


#ifdef __cplusplus
};  /* end of extern "C" */
#endif

#endif /* NV_MAP_H_ */
