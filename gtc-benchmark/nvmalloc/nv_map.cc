#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <strings.h>
#include <time.h>
#include <assert.h>
#include <inttypes.h>
#include <signal.h>
#include "nv_map.h"
#include "list.h"
#include "nv_def.h"
#include "checkpoint.h"
#include "util_func.h"
#include <pthread.h>
#include <sys/time.h>
//#include "time_delay.h"
//#include "snappy.h"

//#define NV_DEBUG
int dummy_var = 0;
/*List containing all project id's */
static struct list_head proc_objlist;
/*intial process obj list var*/
int proc_list_init = 0;
/*fd for file which contains process obj map*/
static int proc_map;
ULONG proc_map_start;
int g_file_desc = -1;
//void *map = NULL;
ULONG tot_bytes =0 ;
nvarg_s nvarg;
rbtree map_tree;
rbtree proc_tree;
//status purpose
UINT total_mmaps;

#define NVRAM_OPTIMIZE

#ifdef NVRAM_OPTIMIZE
proc_s* prev_proc_obj = NULL;
//unsigned prev_proc_id;
#endif

unsigned prev_proc_id;
//#define ASYN_PROC
//
#ifdef _USE_FAULT_PATTERNS
int chunk_fault_lst_freeze;
#endif

//checkpoint related code
pthread_mutex_t chkpt_mutex = PTHREAD_MUTEX_INITIALIZER;
int mutex_set = 0;
pthread_cond_t          dataPresentCondition = PTHREAD_COND_INITIALIZER;
int                     dataPresent=0;


#include <iostream>
#include <string>
#include <map>
#include <algorithm>
#include <functional>
using namespace std;
std::map <int,int> fault_chunk, fault_hist;
std::map <int,int>::iterator fault_itr;
int stop_history_coll, checpt_cnt;





int CompRange(node key_node, void* a, void* b) {

	/*rb_red_blk_node *node = (rb_red_blk_node *)c;*/
    struct mmapobj_nodes *mmapobj_struct=
					 (struct mmapobj_nodes *)key_node->value;

	ULONG a_start_addr = (ULONG)a; 
	ULONG b_start_addr = (ULONG)b; 

	if((a_start_addr > b_start_addr) &&
		( mmapobj_struct->end_addr > a_start_addr))
   	{
#ifdef NV_DEBUG 
		fprintf(stdout, "a_start_addr %lu, b_start_addr %lu," 
				"mmapobj_struct->end_addr %lu mmpaid %d\n",
				a_start_addr, b_start_addr, mmapobj_struct->end_addr,
				mmapobj_struct->map_id);
#endif
			return(0);
	}else{
#ifdef NV_DEBUG
			fprintf(stdout,"Compare range: mapid %u"
					" a_start_addr %lu "
					"b_start_addr %lu "
					"mmapobjstruct.end_addr %lu \n",
					 mmapobj_struct->map_id, 
					 a_start_addr,
					 b_start_addr,
					 mmapobj_struct->end_addr);
#endif
	}
  if(a_start_addr > b_start_addr) return(1);
  if( a_start_addr < b_start_addr) return(-1);
  return(0);
}


int IntComp(node n, void* a, void* b) {
  if( (uintptr_t)a > (uintptr_t)b) return(1);
  if( (uintptr_t)a < (uintptr_t)b) return(-1);
  return(0);
}


void *mmap_wrap(void *addr, size_t size, 
				int mode, int prot, int fd, 
				size_t offset, nvarg_s *a) {


	//return (void *)syscall(__NR_nv_mmap_pgoff,addr,size,mode,prot, &a);
	return	mmap(addr,size,mode,prot,fd,offset);

}


//RBtree code ends
int initialize_mmapobj_tree( proc_s *proc_obj){

	 assert(proc_obj);
	 if(!proc_obj->mmapobj_tree) {
	     proc_obj->mmapobj_tree =rbtree_create();
		 assert(proc_obj->mmapobj_tree);
     	 proc_obj->mmapobj_initialized = 1;
	 }
	return 0;
}

int init_chunk_tree( mmapobj_s *mmapobj){

	 assert(mmapobj);
	 mmapobj->chunkobj_tree =rbtree_create();
     mmapobj->chunk_tree_init = 1;
	 return 0;
}


static char* generate_file_name(char *base_name, int pid, char *dest) {
 int len = strlen(base_name);
 char c_pid[16];
 sprintf(c_pid, "%d", pid);
 memcpy(dest,base_name, len);
 len++;
 strcat(dest, c_pid);
 return dest;
}


void print_mmapobj(mmapobj_s *mmapobj) {

	fprintf(stdout,"----------------------\n");
    fprintf(stdout,"mmapobj: vma_id %u\n", mmapobj->vma_id);
    fprintf(stdout,"mmapobj: length %ld\n",  mmapobj->length);
    fprintf(stdout,"mmapobj: proc_id %d\n", mmapobj->proc_id); 
    fprintf(stdout,"mmapobj: offset %ld\n", mmapobj->offset);
	fprintf(stdout,"mmapobj: numchunks %d \n",mmapobj->numchunks); 
	fprintf(stdout,"----------------------\n");
}

void print_chunkobj(chunkobj_s *chunkobj) {

	fprintf(stdout,"----------------------\n");
    fprintf(stdout,"chunkobj: chunkid %u\n", chunkobj->chunkid);
    fprintf(stdout,"chunkobj: length %ld\n", chunkobj->length);
    fprintf(stdout,"chunkobj: vma_id %d\n", chunkobj->vma_id); 
    fprintf(stdout,"chunkobj: offset %ld\n", chunkobj->offset); 
#ifdef VALIDATE_CHKSM
    fprintf(stdout,"chunkobj: checksum %ld\n",  chunkobj->checksum); 
#endif
	fprintf(stdout,"----------------------\n");
}

int copy_chunkoj(chunkobj_s *dest, chunkobj_s *src) {

	assert(dest);

	dest->chunkid = src->chunkid;
	dest->length =  src->length;
	dest->vma_id = src->vma_id;
	dest->offset = src->offset;

	return 0;
}


int copy_mmapobj(mmapobj_s *dest, mmapobj_s *src) {

	assert(dest);

	dest->vma_id = src->vma_id;
    dest->length = src->length;
    dest->proc_id = src->proc_id;
    dest->offset  = src->offset;
	dest->numchunks = src->numchunks;

	return 0;
}

/*creates mmapobj object.sets object variables to app. value*/
static mmapobj_s* create_mmapobj(rqst_s *rqst, 
				ULONG curr_offset, proc_s* proc_obj) {

	mmapobj_s *mmapobj = NULL;
	ULONG addr = 0;

	assert(rqst);
	assert(proc_obj);

	addr = proc_map_start;
	addr = addr + proc_obj->meta_offset;
	mmapobj = (mmapobj_s*) addr;
	proc_obj->meta_offset += sizeof(mmapobj_s);

	mmapobj->vma_id =  rqst->id;
	mmapobj->length = rqst->bytes;
    mmapobj->proc_id = rqst->pid;
	mmapobj->offset = curr_offset;

	rqst->id = BASE_METADATA_NVID + mmapobj->vma_id;
	rqst->bytes = BASE_METADATA_SZ;
	addr = (ULONG)(map_nvram_state(rqst));
	assert(addr);
	memset((void *)addr,0, BASE_METADATA_SZ);
	mmapobj->strt_addr = addr;

#ifdef NV_DEBUG
	fprintf(stdout, "Setting offset mmapobj->vma_id"
			" %u to %u  %u\n",mmapobj->vma_id, 
			mmapobj->offset, proc_obj->meta_offset);
#endif
	init_chunk_tree(mmapobj);
	assert(mmapobj->chunkobj_tree);
	return mmapobj;
}

/*creates mmapobj object.sets object variables to app. value*/
static chunkobj_s* create_chunkobj(rqst_s *rqst, mmapobj_s* mmapobj) {

	chunkobj_s *chunkobj = NULL;
	ULONG addr = 0;
	UINT mapoffset =0;
	void *ptr;

	assert(rqst);
	mapoffset = mmapobj->meta_offset;
	addr = mmapobj->strt_addr;
	assert(addr);
	addr = addr + mapoffset;
	chunkobj = (chunkobj_s*)addr;
	mapoffset += sizeof(chunkobj_s);
	
	assert(chunkobj);
	chunkobj->chunkid =  rqst->id;
	chunkobj->length = rqst->bytes;
    chunkobj->vma_id = mmapobj->vma_id;
	chunkobj->offset = rqst->offset;
	chunkobj->mmapobj = mmapobj;
	//chunkobj->dirty = 1;

    chunkobj->nv_ptr = rqst->nv_ptr;
	assert(chunkobj->nv_ptr);
    chunkobj->dram_ptr = rqst->dram_ptr;
	chunkobj->dram_sz = rqst->dram_sz;

	 mmapobj->meta_offset = mapoffset;
	 ptr = (void *)(addr + mapoffset);
	 ptr = NULL;	

#ifdef NV_DEBUG
	fprintf(stdout, "Setting chunkid %d vma_id"
			" %u at offset %u and mmap offset %u \n",chunkobj->chunkid, 
			chunkobj->vma_id, chunkobj->offset, mmapobj->meta_offset);
#endif
	return chunkobj;
}


 int  setup_map_file(char *filepath, ULONG bytes)
 {
	int result;
        int fd; 

	fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, (mode_t) 0600);
	if (fd == -1) {
		perror("Error opening file for writing");
		exit(EXIT_FAILURE);
	}

	result = lseek(fd,bytes,  SEEK_SET);
	if (result == -1) {
		close(fd);
		perror("Error calling lseek() to 'stretch' the file");
		exit(EXIT_FAILURE);
	}

	result = write(fd, "", 1);
	if (result != 1) {
		close(fd);
		perror("Error writing last byte of the file");
		exit(EXIT_FAILURE);
	}
	return fd;
}




/*Function to return the process object to which mmapobj belongs
@ mmapobj: process to which the mmapobj belongs
@ return: process object 
 */
proc_s* get_process_obj(mmapobj_s *mmapobj) {

	if(!mmapobj) {
		fprintf(stdout, "get_process_obj: mmapobj null \n");
		return NULL;
	}
	return mmapobj->proc_obj;
}


int  find_vmaid_from_chunk(rbtree_node n, unsigned int chunkid) {

	int ret =0;
	mmapobj_s *t_mmapobj = NULL;
    chunkobj_s *chunkobj = NULL;	
		
	//assert(n);
	if (n == NULL) {
		return 0;
	}

    if (n->right != NULL) {
       	 ret = find_vmaid_from_chunk(n->right, chunkid);
		 if(ret)	
			return ret;
    }

 	t_mmapobj = (mmapobj_s *)n->value;
	
	 if(t_mmapobj) {	
		if(t_mmapobj->chunkobj_tree) {
		 	chunkobj  = (chunkobj_s *)rbtree_lookup(t_mmapobj->chunkobj_tree,
	    		       (void*)chunkid, IntComp);
			if(chunkobj){
				//fprintf(stdout,"t_mmapobj->vma_id %d %lu\n", t_mmapobj->vma_id, (unsigned long)chunkobj);
				return t_mmapobj->vma_id;
			}
		}
	  }

    if (n->left != NULL) {
        return find_vmaid_from_chunk(n->left, chunkid);
    }

	return 0;
}



/*Function to find the mmapobj.
@ process_id: process identifier
@ var:  variable which we are looking for
 */
mmapobj_s* find_mmapobj_from_chunkid(unsigned int chunkid, proc_s *proc_obj ) {

	mmapobj_s *t_mmapobj = NULL;
	ULONG vmid_l = 0;	

	if(!proc_obj) {
		fprintf(stdout, "could not identify project id \n");
		return NULL;
	}
	/*if mmapobj is not yet initialized do so*/
	if (!proc_obj->mmapobj_initialized){
			initialize_mmapobj_tree(proc_obj);
	        return NULL;
	}
	if(proc_obj->mmapobj_tree) {

		//print_tree(proc_obj->mmapobj_tree);
		vmid_l = find_vmaid_from_chunk(proc_obj->mmapobj_tree->root,chunkid);	
		if(!vmid_l){ 
			return NULL;
		}
		t_mmapobj =(mmapobj_s*)rbtree_lookup(proc_obj->mmapobj_tree,
					(void *)vmid_l, IntComp);
	}
 #ifdef NV_DEBUG
        if(t_mmapobj)
	       fprintf(stdout, "find_mmapobj chunkid %u in vmaid %u \n", chunkid, vmid_l);
#endif
	return t_mmapobj;
}


mmapobj_s* find_mmapobj(UINT vmid_l, proc_s *proc_obj ) {

	mmapobj_s *t_mmapobj = NULL;
	if(!proc_obj) {
		fprintf(stdout, "could not identify project id \n");
		return NULL;
	}
	/*if mmapobj is not yet initialized do so*/
	if (!proc_obj->mmapobj_initialized){
			initialize_mmapobj_tree(proc_obj);
	        return NULL;
	}
	t_mmapobj =(mmapobj_s*)rbtree_lookup(proc_obj->mmapobj_tree,
				(void *)vmid_l, IntComp);
 #ifdef NV_DEBUG
        if(t_mmapobj)
	       fprintf(stdout, "find_mmapobj found t_mmapobj %u \n", t_mmapobj->vma_id);
#endif
	return t_mmapobj;
}

/*add the mmapobj to process object*/
static int add_mmapobj(mmapobj_s *mmapobj, proc_s *proc_obj) {

    if (!mmapobj)
       return 1;

    if (!proc_obj)
    		return -1;

    if (!proc_obj->mmapobj_initialized)
    	initialize_mmapobj_tree(proc_obj);

    //RB tree code
    assert(proc_obj->mmapobj_tree);
	rbtree_insert(proc_obj->mmapobj_tree, (void*)mmapobj->vma_id,
				 mmapobj, IntComp);
    //set the process obj to which mmapobj belongs 
    mmapobj->proc_obj = proc_obj;
    return 0;
}

/*add the mmapobj to process object*/
static int add_chunkobj(mmapobj_s *mmapobj, chunkobj_s *chunk_obj) {

    if (!mmapobj)
       return -1;

    if (!chunk_obj)
    		return -1;
#ifdef NV_DEBUG
	fprintf(stdout,"add_chunkobj: chunkid %d"
			"chunk_tree_init %d \n",(void*)chunk_obj->chunkid,
			mmapobj->chunk_tree_init);
#endif

    if (!mmapobj->chunk_tree_init){
    	init_chunk_tree(mmapobj);
	}
	assert(mmapobj->chunkobj_tree);
	rbtree_insert(mmapobj->chunkobj_tree, (void*)(chunk_obj->chunkid),
				 (void*)chunk_obj, IntComp);

    //set the process obj to which mmapobj belongs 
    //chunk_obj->mmapobj = mmapobj;
    return 0;
}

int restore_chunk_objs(mmapobj_s *mmapobj, int perm){

	rqst_s rqst;
	chunkobj_s *nv_chunkobj = NULL;
	chunkobj_s *chunkobj = NULL;

	void *mem = NULL;
	int idx = 0;
	void *addr = NULL;

	assert(mmapobj);
	rqst.id = BASE_METADATA_NVID + mmapobj->vma_id;
	rqst.pid = mmapobj->proc_id;
	rqst.bytes = mmapobj->length;
#ifdef NV_DEBUG
	print_mmapobj(mmapobj);
#endif
    mem = map_nvram_state(&rqst);	
	assert(mem);

	record_metadata_vma(rqst.id, rqst.bytes);

	mmapobj->strt_addr = (unsigned long)mem;
	addr = (void *)mem;
    mmapobj->chunk_tree_init = 0;	

	for (idx = 0; idx < mmapobj->numchunks; idx++) {

	    nv_chunkobj = (chunkobj_s*)addr;

		if(check_modify_access(perm)) {
			chunkobj = nv_chunkobj;
		}else {
			
			chunkobj = (chunkobj_s *)malloc(sizeof(chunkobj_s));
			assert(chunkobj);	
			copy_chunkoj(chunkobj,nv_chunkobj);
		}

	    chunkobj->dram_ptr =malloc(chunkobj->length);
		chunkobj->nv_ptr = 0;
		chunkobj->nv_ptr = 0;

		if(add_chunkobj(mmapobj, chunkobj))
			goto error;
		addr = addr + sizeof(chunkobj_s);

		assert(chunkobj->dram_ptr);
        record_chunks(chunkobj->dram_ptr,chunkobj); 

#ifdef NV_DEBUG
        print_chunkobj(chunkobj);
#endif
		//print_mmapobj(mmapobj);
        //print_chunkobj(chunkobj);
	}
	return 0;
error:
	return -1;

}


/*Idea is to have a seperate process map file for each process
 But fine for now as using browser */
static proc_s * create_proc_obj(int pid) {

      proc_s *proc_obj = NULL;
      size_t bytes = sizeof(proc_s);
      char file_name[256];
       
     
      bzero(file_name, 256);
      generate_file_name(MAPMETADATA_PATH, pid, file_name);
      proc_map = setup_map_file(file_name, METADAT_SZ);
      if (proc_map < 1) {
          printf("failed to create a map using file %s\n",
				file_name);
          return NULL;
      }

        proc_obj = (proc_s *) mmap(0, METADAT_SZ, PROT_NV_RW,
                        MAP_SHARED, proc_map, 0);

        if (proc_obj == MAP_FAILED) {
                close(proc_map);
                perror("Error mmapping the file");
                exit(EXIT_FAILURE);
        }
        memset ((void *)proc_obj,0,bytes);
        proc_map_start = (ULONG) proc_obj;
        return proc_obj;
}



/*Func resposible for locating a process object given
 process id. The idea is that once we have process object
 we can get the remaining mmaps allocated process object
 YET TO COMPLETE */
static proc_s *find_proc_obj(int proc_id) {

    proc_s *proc_obj = NULL;

#ifdef NVRAM_OPTIMIZE
	if(proc_id && ((UINT)proc_id == prev_proc_id)  ) {	
		
		if(prev_proc_obj) {
#ifdef NV_DEBUG
			fprintf(stdout, "returning from cache \n");
#endif
			return prev_proc_obj;
		}
	}
#endif


    if (!proc_list_init) {
       //INIT_LIST_HEAD(&proc_objlist);
		proc_tree = rbtree_create();
        proc_list_init = 1;
        return NULL;
    }

	proc_obj = (proc_s *)rbtree_lookup(
				proc_tree,(void *)proc_id,IntComp);
	return proc_obj;
}



ULONG initialized =0;


/*Every NValloc call creates a mmap and each mmap
 is added to process object list*/
mmapobj_s* add_mmapobj_to_proc(proc_s *proc_obj, 
						rqst_s *rqst, ULONG offset) {

        mmapobj_s *mmapobj = NULL;

        mmapobj = create_mmapobj( rqst, offset, proc_obj);
		assert(mmapobj);

		/*record the vma id */
		record_metadata_vma(mmapobj->vma_id, mmapobj->length);

        /*add mmap to process object*/
          add_mmapobj(mmapobj, proc_obj);
          proc_obj->num_mmapobjs++;
#ifdef NV_DEBUG
		fprintf(stdout, "proc_obj->num_mmapobjs %d \n",proc_obj->num_mmapobjs);	
		print_mmapobj(mmapobj);
#endif
        return mmapobj;
}

static int add_chunk_to_mmapobj(mmapobj_s *mmapobj, proc_s *proc_obj,
								rqst_s *rqst) {

        chunkobj_s *chunkobj = NULL;
        chunkobj = create_chunkobj( rqst, mmapobj);
		assert(chunkobj);

        /*add mmap to process object*/
        add_chunkobj(mmapobj,chunkobj);

		assert(chunkobj->dram_ptr);
		record_chunks(chunkobj->dram_ptr,chunkobj);

        mmapobj->numchunks++;
#ifdef NV_DEBUG
	    print_chunkobj(chunkobj);	
#endif
        return 0;
}





/*add process to the list of processes*/
static int add_proc_obj(proc_s *proc_obj) {

        if (!proc_obj)
                return 1;
   
       //if proecess list is not initialized,
       //then intialize it
       if (!proc_list_init) {
    	    //INIT_LIST_HEAD(&proc_objlist);
			proc_tree = rbtree_create(); 
       		proc_list_init = 1;
	    }	
        //list_add(&proc_obj->next_proc, &proc_objlist);

		rbtree_insert(proc_tree,(void *)proc_obj->pid,proc_obj, IntComp);

#ifdef NV_DEBUG
        fprintf(stdout,"add_proc_obj:"
				"proc_obj->pid %d \n", proc_obj->pid);
#endif
        return 0;
}

/*Function to find the process.
@ pid: process identifier
 */
proc_s* find_process(int pid) {

    proc_s* t_proc_obj = NULL;

    if(!proc_list_init){
    	return NULL;
    }
	t_proc_obj = (proc_s*)rbtree_lookup(proc_tree,(void *)pid, IntComp);

 #ifdef NV_DEBUG
        if(t_proc_obj)
       fprintf(stdout, "find_mmapobj found t_mmapobj %u \n", t_proc_obj->pid);
#endif
    return t_proc_obj;

}

//Return the starting address of process
ULONG  get_proc_strtaddress(rqst_s *rqst){

	proc_s *proc_obj=NULL;
    int pid = -1;
    uintptr_t uptrmap;

    pid = rqst->pid;
	proc_obj = find_proc_obj(pid);
    if (!proc_obj) {
		fprintf(stdout,"could not find the process. Check the pid %d\n", pid);
		return 0;	
    }else {
		//found the start address
        uptrmap = (uintptr_t)proc_obj->start_addr;
		return proc_obj->start_addr;
    }
	return 0;
}

//Temporary memory allocation
//CAUTION: returns 0 if success
int nv_initialize(rqst_s *rqst) {

	int pid = -1;
	proc_s *proc_obj=NULL;
	ULONG bytes = 0;
	char *var = NULL;
	char file_name[256];

#ifdef NV_DEBUG
    fprintf(stdout,"Entering nv_initialize\n");
#endif

	bzero(file_name,256);

	if( !rqst ) {
		return -1;
	}

	bytes = rqst->bytes;
	var = (char *)rqst->var_name;
	pid = rqst->pid;

    proc_obj = find_proc_obj(pid);
	if(proc_obj) {
		proc_map_start = (ULONG)proc_obj;
	}
	if (!proc_obj) {
		proc_obj = create_proc_obj(rqst->pid);

        if(proc_obj) {
			proc_obj->pid = rqst->pid;
			proc_obj->size = 0;
			proc_obj->num_mmapobjs = 0;
			proc_obj->start_addr = 0;
			proc_obj->offset = 0;
            proc_obj->meta_offset = sizeof(proc_s);
   			add_proc_obj(proc_obj);
        }
		assert(proc_obj);

		/*FIX ME: Something wrong*/
		generate_file_name((char *) FILEPATH, rqst->pid, file_name);
		g_file_desc = setup_map_file(file_name, MAX_DATA_SIZE);
        proc_obj->file_desc = g_file_desc;
	}
#ifdef NV_DEBUG
	fprintf(stdout,"proc_obj->offset %ld \n", proc_obj->offset);
#endif

	if (!proc_obj){
		fprintf(stdout,"process object does not exist\n");
		return -1;
    }
	proc_obj->data_map_size += bytes;

	//For now return 0; 
	//else caller will complain
	return 0;
}


//Temporary memory allocation
//CAUTION: returns 0 if success
//earlier this method was nv_mmapobj
void* create_new_process(rqst_s *rqst) {

	int pid = -1;
	proc_s *proc_obj=NULL;
	ULONG bytes = 0;
	char *var = NULL;
	char file_name[256];

	assert(rqst);
	bytes = rqst->bytes;
	var = (char *)rqst->var_name;
	pid = rqst->pid;

	proc_obj = create_proc_obj(rqst->pid);
	assert(proc_obj);
	proc_obj->pid = rqst->pid;
	proc_obj->size = 0;
	proc_obj->num_mmapobjs = 0;
	proc_obj->start_addr = 0;
	proc_obj->offset = 0;
    proc_obj->meta_offset = sizeof(proc_s);
    add_proc_obj(proc_obj);

	/*FIX ME: Something wrong*/
	bzero(file_name,256);
	generate_file_name((char *) FILEPATH, rqst->pid, file_name);
	g_file_desc = setup_map_file(file_name, MAX_DATA_SIZE);
    proc_obj->file_desc = g_file_desc;
	proc_obj->data_map_size += bytes;

#ifdef NV_DEBUG
	fprintf(stdout,"proc_obj->offset %ld \n", proc_obj->offset);
	fprintf(stdout,"mmapobjing again %lu \n",bytes);
#endif
	//For now return 0; 
	//else caller will complain
	return (void *)proc_obj;
}



/*Function to copy mmapobjs */
static int mmapobj_copy(mmapobj_s *dest, mmapobj_s *src){ 

	if(!dest ) {
		printf("mmapobj_copy:dest is null \n");
		goto null_err;
	}

	if(!src ) {
		printf("mmapobj_copy:src is null \n");
		goto null_err;
	}

	//TODO: this is not a great way to copy structures
	dest->vma_id = src->vma_id;
	dest->length = src->length;
	dest->proc_id = src->proc_id;
	dest->offset = src->offset;
    //print_mmapobj(dest);
    
        
#ifdef CHCKPT_HPC
	//FIXME:operations should be structure
	// BUT how to map them
    dest->order_id = src->order_id;  
	dest->ops = src->ops;
#endif

	return 0;

	null_err:
	return -1;	
}





/*gives the offset from start address
 * @params: start_addr start address of process
 * @params: curr_addr  curr_addr address of process
 * @return: offset
 */
ULONG findoffset(UINT proc_id, ULONG curr_addr) {

	proc_s *proc_obj = NULL;
	ULONG diff = 0;

	proc_obj = find_proc_obj(proc_id);
	if (proc_obj) {
		diff = curr_addr - (ULONG)proc_obj->start_addr;
		return diff;
	}
    return 0;
}

/* update offset for a mmapobj relative to start address
 * @params: proc_id
 * @params: vma_id
 * @params: offset
 * @return: 0 if success, -1 on failure
 */
int nv_record_chunk(rqst_s *rqst, ULONG addr) {

	proc_s *proc_obj;
	mmapobj_s* mmapobj = NULL;
	long vma_id = 0;
	UINT proc_id =0, offset =0;
	ULONG start_addr=0;
	rqst_s lcl_rqst;
  

	assert(rqst);
	proc_id = rqst->pid;
	proc_obj = find_proc_obj(proc_id);

	if(!proc_obj)
		proc_obj = ( proc_s *)create_new_process(rqst);	
	assert(proc_obj);

	vma_id = locate_mmapobj_node((void *)addr, rqst, &start_addr);
	assert(vma_id);
	assert(start_addr);

	//fprintf(stderr,"adding chunk %d of size %u:"
	//				"to vma_id %u\n",lcl_rqst.id, 
	//				lcl_rqst.bytes,vma_id);
	/*first we try to locate an exisiting obhect*/
	/*add a new mmapobj and update to process*/
	mmapobj = find_mmapobj( vma_id, proc_obj );
	lcl_rqst.pid = rqst->pid;
	lcl_rqst.id = vma_id;
	lcl_rqst.bytes =get_vma_size(vma_id);
	assert(lcl_rqst.bytes > 0);

	if(!mmapobj) {
		mmapobj = add_mmapobj_to_proc(proc_obj, &lcl_rqst, offset);
		assert(mmapobj);
	}

	offset = addr - start_addr;
	prev_proc_id = rqst->pid;

	/*find the mmapobj using vma id
    if application has supplied request id, neglect*/
    if(rqst->var_name) {     
		lcl_rqst.id = gen_id_from_str(
						rqst->var_name);

#ifdef NV_DEBUG
		
		if(rqst->pid == 1)
		fprintf(stdout,"generated chunkid %u from "
			"variable %s rqst->dram_ptr %lu \n",
			lcl_rqst.id, rqst->var_name,
			rqst->dram_ptr);
#endif
    }
    else{
	   lcl_rqst.id = rqst->id;
    }
	assert(lcl_rqst.id);
	lcl_rqst.nv_ptr = (void *)addr;
	lcl_rqst.dram_ptr = rqst->dram_ptr;
	lcl_rqst.bytes = rqst->bytes;	
    lcl_rqst.offset = offset;
	lcl_rqst.dram_sz = rqst->dram_sz;	
	add_chunk_to_mmapobj(mmapobj, proc_obj, &lcl_rqst);

#ifdef NV_DEBUG
	fprintf(stderr,"adding chunk %d of size %u:"
					"to vma_id %u\n",lcl_rqst.id, 
					lcl_rqst.bytes,vma_id);
#endif
	return SUCCESS;
}


/*if not process with such ID is created then
we return 0, else number of mapped blocks */
int get_proc_num_maps(int pid) {

    proc_s *proc_obj = NULL;
	proc_obj = find_proc_obj(pid);
	if(!proc_obj) {
		fprintf(stdout,"process not created \n");
		return 0;
	}
	else {
	    //also update the number of mmapobj blocks
    	return proc_obj->num_mmapobjs;
	}
	return 0;
}


int  iterate_chunk_obj(rbtree_node n) {

    chunkobj_s *chunkobj = NULL;	
		
	if (n == NULL) {
		return 0;
	}

    if (n->right != NULL) {
	  	 iterate_chunk_obj(n->right);
    }

 	chunkobj = (chunkobj_s *)n->value;
	if(chunkobj) {	
		fprintf(stdout,"chunkobj chunkid: %d addr %lu\n", 
				chunkobj->chunkid, (unsigned long)chunkobj);
		return 0;
	}

    if (n->left != NULL) {
       iterate_chunk_obj(n->left);
    }
	return 0;
}



int  iterate_mmap_obj(rbtree_node n) {

	mmapobj_s *t_mmapobj = NULL;
		
	if (n == NULL) {
		return 0;
	}

    if (n->right != NULL) {
	  	 iterate_mmap_obj(n->right);
    }

 	t_mmapobj = (mmapobj_s *)n->value;
	
	 if(t_mmapobj) {	
		if(t_mmapobj->chunkobj_tree) {
			iterate_chunk_obj(t_mmapobj->chunkobj_tree->root);
		}
	  }

    if (n->left != NULL) {
       iterate_mmap_obj(n->left);
    }
	return 0;
}

proc_s* load_process(int pid, int perm) {

	proc_s *proc_obj = NULL;
	proc_s *nv_proc_obj = NULL;

	size_t bytes = 0;
	int fd = -1;
	void *map;
	int idx = 0;
	ULONG addr = 0;

	mmapobj_s *mmapobj, *nv_mmapobj;

	char file_name[256];

#ifdef NV_DEBUG
     fprintf(stdout, "In load_process"
		  	"for pid: %d \n", pid);
#endif

	bzero(file_name,256);
	bytes = sizeof(proc_s);
	generate_file_name((char *) MAPMETADATA_PATH, pid, file_name);
	fd = open(file_name, O_RDWR);
	if (fd == -1) {
		perror("Error opening file for reading");
		return NULL;
	}
	map = (proc_s *) mmap(0, METADAT_SZ,
			PROT_NV_RW, MAP_SHARED, fd, 0);

	nv_proc_obj = (proc_s *) map;
	if (nv_proc_obj == MAP_FAILED) {
		close(fd);
		perror("Error mmapobjping the file");
		return NULL;
	}
	//Start reading the mmapobjs
	//add the process to proc_obj tree
	//initialize the mmapobj list if not
	addr = (ULONG) nv_proc_obj;
	addr = addr + sizeof(proc_s);


	if(check_modify_access(perm)){

		fprintf(stdout,"WRITE permission\n");


		proc_obj = nv_proc_obj;

	}else {

		fprintf(stdout,"process does not have permission\n");

	    proc_obj = (proc_s *)malloc(sizeof(proc_s));
		assert(proc_obj);

		proc_obj->pid = nv_proc_obj->pid;
		proc_obj->size = nv_proc_obj->size;
		proc_obj->num_mmapobjs = nv_proc_obj->num_mmapobjs;
		proc_obj->start_addr = 0;
	}

	proc_obj->mmapobj_initialized = 0;
	proc_obj->mmapobj_tree = 0;


    add_proc_obj(proc_obj);
    if (!proc_obj->mmapobj_initialized){
       initialize_mmapobj_tree(proc_obj);
     }
#ifdef NV_DEBUG
	fprintf(stdout,"proc_obj->pid %d \n", proc_obj->pid);
	fprintf(stdout,"proc_obj->size %lu \n",proc_obj->size);
	fprintf(stdout,"proc_obj->num_mmapobjs %d\n", proc_obj->num_mmapobjs);
	fprintf(stdout,"proc_obj->start_addr %lu\n", proc_obj->start_addr);
#endif
	//Read all the mmapobjsa
	for (idx = 0; idx < proc_obj->num_mmapobjs; idx++) {

		nv_mmapobj = (mmapobj_s*) addr;

		if(check_modify_access(perm)){
	
			mmapobj = nv_mmapobj;
		}else {

			 mmapobj = (mmapobj_s *)malloc(sizeof(mmapobj_s));
			 copy_mmapobj(mmapobj, nv_mmapobj);
		}

		add_mmapobj(mmapobj, proc_obj);
		if(restore_chunk_objs(mmapobj, perm)){
			fprintf(stdout, "failed restoration\n");
			goto error;
		}

#ifdef ENABLE_CHECKPOINT	
		if(mmapobj)	
			record_vmas(mmapobj->vma_id, mmapobj->length);	
#endif
		addr = addr + sizeof(mmapobj_s);
#ifdef NV_DEBUG
        print_mmapobj(mmapobj);
		//print_tree(mmapobj->chunkobj_tree);
#endif
	}

	return proc_obj;

error:
	return NULL;
}

//This function just maps the address space corresponding
//to process.
void* map_nvram_state(rqst_s *rqst) {

	void *nvmap = NULL;
	int fd = -1;  	
	nvarg_s a;
    a.proc_id = rqst->pid;
    assert(a.proc_id);
    a.fd = -1;
    a.vma_id =rqst->id;
    a.pflags = 1;
    a.noPersist = 0;
#ifdef NV_DEBUG
	printf("nvarg.proc_id %d %d %d\n",rqst->bytes, rqst->id, a.proc_id);
#endif
	nvmap = mmap_wrap(0,rqst->bytes,PROT_NV_RW, PROT_ANON_PRIV,fd,0, &a);
    assert(nvmap);

	if (nvmap == MAP_FAILED) {
       close(fd);
       goto error;
	}
	return nvmap;
error:
    return NULL;
}

void* nv_map_read(rqst_s *rqst, void* map ) {

    ULONG offset = 0;
    int process_id = 1;
    proc_s *proc_obj = NULL;
    unsigned int chunk_id;
    mmapobj_s *mmapobj_ptr = NULL;
	void *map_read = NULL;
	rbtree_t *tree_ptr;
	chunkobj_s *chunkobj;

	int perm = rqst->access;

#ifdef VALIDATE_CHKSM
    char gen_key[256];
	long hash;
#endif


    process_id = rqst->pid;
   //Check if all the process objects are still in memory and we are not reading
   //process for the first time
   proc_obj = find_process(rqst->pid);
   if(!proc_obj) {
        /*looks like we are reading persistent structures 
		and the process is not avaialable in memory
	    FIXME: this just addressies one process,
		since map_read field is global*/
	    proc_obj = load_process(process_id, perm);
    	if(!proc_obj){
#ifdef NV_DEBUG
	       printf("proc object for %d failed\n", process_id);
#endif
    	   goto error;
	    }
	}

    if(rqst->var_name){
		chunk_id = gen_id_from_str(rqst->var_name);
	}
    else{
      chunk_id = rqst->id;
	} 

    mmapobj_ptr = find_mmapobj_from_chunkid( chunk_id, proc_obj );
    if(!mmapobj_ptr) {
        //fprintf(stdout,"finding mmapobj %u for proc"
		//		"%d failed \n",chunk_id, process_id);
		goto error;        
    }
	
     rqst->id = mmapobj_ptr->vma_id;
     rqst->pid =mmapobj_ptr->proc_id;
	 rqst->bytes= mmapobj_ptr->length;
     map_read = map_nvram_state(rqst);

     if(!map_read){
    	 fprintf(stdout, "nv_map_read:"
				  " map_process returned null \n");
    	 goto error;
     }
    //Get the the start address and then end address of mmapobj
	/*Every malloc call will lead to a mmapobj creation*/
	tree_ptr = mmapobj_ptr->chunkobj_tree;	
	chunkobj  = (chunkobj_s *)rbtree_lookup(tree_ptr,
                       (void*)chunk_id, IntComp);
	assert(chunkobj);
    offset = chunkobj->offset;
	chunkobj->nv_ptr = map_read+ offset;
    chunkobj->dram_ptr = malloc(chunkobj->length);
	assert(chunkobj->dram_ptr);
    memcpy(chunkobj->dram_ptr, chunkobj->nv_ptr, chunkobj->length);

#ifdef VALIDATE_CHKSM
    bzero(gen_key, 256);
    sha1_mykeygen(chunkobj->dram_ptr, gen_key,
                CHKSUM_LEN, 16, chunkobj->length);
	hash = gen_id_from_str(gen_key);
	if(hash != chunkobj->checksum){
		//fprintf(stdout,"CHUNK CORRUPTION \n");
		print_chunkobj(chunkobj);	
		//goto error;
	}else {

	}
#endif


#ifdef NV_DEBUG
     fprintf(stdout, "nv_map_read: mmapobj offset"
			 "%lu %lu %u \n", offset, 
			 (ULONG)map_read, 
			 mmapobj_ptr->vma_id);
#endif
   rqst->nv_ptr = chunkobj->nv_ptr;   
   rqst->dram_ptr= chunkobj->dram_ptr;

   return (void *)rqst->nv_ptr;
error:
    rqst->nv_ptr = NULL;
    rqst->dram_ptr = NULL;
    return NULL;
}

int nv_munmap(void *addr){

    int ret_val = 0;

	if(!addr) {
		perror("null address \n");
		return -1;
	}
   ret_val = munmap(addr, MAX_DATA_SIZE);
   return ret_val;
}




void *create_map_tree() {

	if(!map_tree)
		  map_tree =rbtree_create();	

	if(!map_tree){
		perror("RB tree creation failed \n");
		exit(-1);
	}
	return map_tree;
}

int insert_mmapobj_node(ULONG val, size_t size, int id, int proc_id) {

	struct mmapobj_nodes *mmapobj_struct;
	//rb_red_blk_node *node = NULL;
	mmapobj_struct = (struct mmapobj_nodes*)malloc(sizeof(struct mmapobj_nodes));
    mmapobj_struct->start_addr = val;
	mmapobj_struct->end_addr = val + size;
	mmapobj_struct->map_id = id;
	mmapobj_struct->proc_id= proc_id;

	if(!map_tree)
		create_map_tree();
#ifdef NV_DEBUG
	fprintf(stdout,"before insert mapid %u start_addr %lu "
					"end_addr %lu, proc_id %d  map_tree %lu \n",
					 mmapobj_struct->map_id, 
					 mmapobj_struct->start_addr,
					 mmapobj_struct->end_addr,
					 mmapobj_struct->proc_id, (ULONG)map_tree);
#endif
	//node = RBTreeInsert( map_tree,&val, mmapobj_struct);
	rbtree_insert(map_tree,(void*)val,mmapobj_struct, IntComp);
	// print_tree(map_tree);
	return 0;
}

int locate_mmapobj_node(void *addr, rqst_s *rqst, ULONG *map_strt_addr){

    struct mmapobj_nodes *mmapobj_struct = NULL;
	ULONG addr_long, strt_addr;
	unsigned int mapid;

	addr_long = (ULONG)addr;
	//print_tree(map_tree);
	mmapobj_struct = (struct mmapobj_nodes *)rbtree_lookup(map_tree,(void *)addr_long,
					CompRange);
	if(mmapobj_struct) {
		mapid = mmapobj_struct->map_id;
		strt_addr = mmapobj_struct->start_addr;
#ifdef NV_DEBUG
		fprintf(stdout,"addr: %lu, query start:%lu, end %lu mapid %d"
						"map_tree %lu\n",
						(ULONG)addr,strt_addr, 
						mmapobj_struct->end_addr,
						mapid, (ULONG)map_tree);
#endif
		*map_strt_addr= strt_addr;
		
		return mapid;
	}
		//print_tree(map_tree);
#ifdef NV_DEBUG
		fprintf(stdout,"query failed pid:%d %u addr: %lu\n", 
				rqst->pid, rqst->id, addr_long);
#endif
	return 0;
}




int id = 0;
size_t total_size =0;
void* _mmap(void *addr, size_t size, int mode, int prot, 
			int fd, int offset, nvarg_s *a){

	void *ret = NULL;
	ULONG addr_long=0;

	assert(a);
	assert(a->proc_id);
    a->fd = -1;
    a->vma_id = ++id;
    a->pflags = 1;
    a->noPersist = 0;
	//ret = mmap(addr, size, mode , prot, fd, offset);
	total_size += size;
	total_mmaps++;
	//fprintf(stdout, "mmap size %u a->proc_id %d"
	//				"vmaid %d\n",size, a->proc_id, a->vma_id);
	ret = mmap_wrap(addr,size, mode, prot,fd,offset, a); 
	assert(ret);
	addr_long = (ULONG)ret;
	insert_mmapobj_node(addr_long, size, id, a->proc_id);			

#ifdef ENABLE_CHECKPOINT
	record_vmas(a->vma_id, size);
#endif

	return ret;
}

#ifdef ENABLE_CHECKPOINT


int reg_for_signal(int procid) {
	return register_ckpt_lock_sig(procid, SIGUSR1);
}


int init_checkpoint(int procid) {

	init_shm_lock(procid);

	//pthread_mutex_init(&chkpt_mutex, NULL);
	//pthread_mutex_lock(&chkpt_mutex);
	//mutex_set = 1;

}

//checkpoint related code
void* proc_rmt_chkpt(int procid, size_t *bytes, int check_dirtypgs) {

	void *ret = NULL;
	proc_s *proc_obj;
	int perm = 0; 

#ifdef ASYN_PROC
	while(wait_for_chkpt_sig(procid, SIGUSR1) == -1){
		sleep(1);
	};
#else
	pthread_mutex_lock(&chkpt_mutex);
#endif


#ifdef NV_DEBUG
    fprintf(stdout, "acquiring spin lock\n");     
#endif


#ifndef ASYN_PROC	
	while (!get_dirtyflag(procid)) {
		pthread_cond_wait(&dataPresentCondition, &chkpt_mutex);
	}

#else
	if(acquire_chkpt_lock(procid)){
		fprintf(stdout,"ckpt failed for %d \n",procid);
		return NULL;
	}

	while (!get_dirtyflag(procid)) {
		sleep(1);
	}
#endif

	proc_obj  = find_process(procid);
	if(!proc_obj) {
		proc_obj = load_process(procid, perm);
		if(!proc_obj){
			fprintf(stdout,"proc_rmt_chkpt: reading proc"
					"%d info failed \n", procid);
			goto exit;
		}
	}
	ret = copy_proc_checkpoint(procid, bytes, 
								check_dirtypgs);
#ifdef NV_DEBUG
    fprintf(stdout, "all checkpoint data ready\n");
#endif
	disable_ckptdirtflag(procid);
exit:

#ifdef ASYN_PROC
	disable_chkpt_lock(procid);
#else
	 pthread_mutex_unlock(&chkpt_mutex);
#endif

	return ret;
}


int  chkpt_all_chunks(rbtree_node n, int *cmt_chunks) {

	int ret =-1;
    chunkobj_s *chunkobj = NULL;
	char gen_key[256];
	ULONG lat_ns, cycles;
	

#ifdef VALIDATE_CHKSM	
	long hash;
#endif
	
	if (n == NULL) {
		return 0;
	}

    if (n->right != NULL) {
       	 ret = chkpt_all_chunks(n->right,cmt_chunks);
    }

 	chunkobj = (chunkobj_s *)n->value;

	if(chunkobj) {	

#ifdef _ASYNC_LCL_CHK
		if(chunkobj->dirty) {
#endif
			void *src = chunkobj->dram_ptr;
			void *dest = chunkobj->nv_ptr;
		
			assert(src);
			assert(dest);
			assert(chunkobj->length);
		

#ifdef NV_DEBUG
		if(prev_proc_id == 1)
				fprintf(stdout,"\n");
#endif
//#ifdef NV_DEBUG

			if(prev_proc_id == 1)
				fprintf(stdout,"commiting chunk %d "
            	    "and size %u \t"
					"commited? %d \n",
					chunkobj->chunkid,
					chunkobj->length,
					chunkobj->dirty);

//#endif
				*cmt_chunks = *cmt_chunks + 1;


#ifdef _NVSTATS	
			struct timeval  memcpy_start,memcpy_end;
			gettimeofday(&memcpy_start, NULL);
#endif

#ifdef _ASYNC_LCL_CHK
			chunkobj->dirty = 0;
#endif

			memcpy_delay(dest,src,chunkobj->length);
		    //Compress(src, chunkobj->length, dest);	
#ifdef _NVSTATS
			gettimeofday(&memcpy_end, NULL);
			if(prev_proc_id == 1)
			fprintf(stdout, "memcpy time%ld %u\n",
					simulation_time( memcpy_start,memcpy_end),
					chunkobj->length);
#endif


#ifdef VALIDATE_CHKSM
    	    bzero(gen_key, 256);
        	sha1_mykeygen(src, gen_key,
					 CHKSUM_LEN, 16, chunkobj->length);	

			hash = gen_id_from_str(gen_key);
	
			 chunkobj->checksum = hash;		
#endif
			ret = 0;

#ifdef _ASYNC_LCL_CHK
		 }
#endif

	}

    if (n->left != NULL) {
       return chkpt_all_chunks(n->left,cmt_chunks);
    }
	return ret;
}

int  chkpt_all_vmas(rbtree_node n) {

	int ret =-1;
	mmapobj_s *t_mmapobj = NULL;
	rbtree_node root;
	int cmt_chunks =0, tot_chunks=0;
		
	//assert(n);
	if (n == NULL) {
		return 0;
	}

    if (n->right != NULL) {
       	 ret = chkpt_all_vmas(n->right);
    }

 	t_mmapobj = (mmapobj_s *)n->value;
	
	 if(t_mmapobj) {	
		if(t_mmapobj->chunkobj_tree) {

			root = t_mmapobj->chunkobj_tree->root;
			if(root)
			ret = chkpt_all_chunks(root, &cmt_chunks);
	    }
	 }

    if (n->left != NULL) {
        return chkpt_all_vmas(n->left);
    }

	tot_chunks = get_chnk_cnt_frm_map();

	//fprintf(stdout,"total chunks %d, cmt chunks %d\n",
	//		tot_chunks, cmt_chunks);
	return ret;
}


int nv_chkpt_all(rqst_s *rqst, int remoteckpt) {

	int process_id = -1;
	proc_s *proc_obj= NULL;   
	rbtree_node root;
	int ret = -1;

	if(!rqst)
		goto error;

#ifdef NV_DEBUG
	fprintf(stdout,"invoking commit "
			"for process %d \n",
			rqst->pid);
#endif

	process_id = rqst->pid;
	proc_obj= find_proc_obj(process_id);

	if(!proc_obj) 
		goto error;

	if(!(proc_obj->mmapobj_tree))
		goto error;

	root = proc_obj->mmapobj_tree->root;
	if(!root) 
		goto error;

	//set_acquire_chkpt_lock(process_id);
	ret = chkpt_all_vmas(root);
	set_ckptdirtflg(process_id);
	//disable_chkpt_lock(process_id);
	//
	//
#ifdef _FAULT_STATS
	if(prev_proc_id == 1)
	set_chunkprot();
#endif //_FAULT_STATS


#ifdef _ASYNC_LCL_CHK
	set_chunkprot();
	
	if(checpt_cnt ==1)
		stop_history_coll = 1;
	clear_fault_lst();
	checpt_cnt++;
#endif



#ifdef ASYN_PROC
	if(remoteckpt)
		send_lock_avbl_sig(SIGUSR1);
#else
	   pthread_cond_signal(&dataPresentCondition);
	   //pthread_mutex_unlock(&chkpt_mutex);
#endif


#ifdef _USE_FAULT_PATTERNS
   if(!check_chunk_fault_lst_empty())
		chunk_fault_lst_freeze =1;
	//if(prev_proc_id == 1)
	//	print_chunk_fault_lst();
#endif

	if(!ret){
#ifdef NV_DEBUG
		printf("nv_chkpt_all: succeeded for procid"
				" %d \n",proc_obj->pid); 
#endif
		return ret;
	}else{
#ifdef NV_DEBUG
		printf("nv_chkpt_all: failed for procid"
				" %d \n",proc_obj->pid); 
#endif
	}
	return ret;

	error:
	return -1;
}

#endif


int nv_commit(rqst_s *rqst) {

	int process_id = -1;
	ULONG addr = 0;
	int size = 0;
	int ops = -1;
	char *var = NULL;
	void *src = NULL;
	proc_s *proc_obj= NULL;   
    unsigned int vma_id = 0;
    mmapobj_s *mmapobj_ptr= NULL;

	if(!rqst)
		return -1;

	var = (char *)rqst->var_name;
	size = rqst->bytes;
	process_id = rqst->pid;
	ops = rqst->ops;
	src = rqst->nv_ptr;
	proc_obj= find_proc_obj(process_id);

#ifdef NV_DEBUG
		fprintf(stdout, "nv_commit: finding mmapobj \n");
#endif
	//find the mmapobj
    //if application has supplied request id, neglect
    if(!rqst->id) {
      if(rqst->var_name) 
	      vma_id = gen_id_from_str(rqst->var_name);
      else
		printf("nv_commit:error generating vma id \n");
    } 
    else
      vma_id = rqst->id;
 
	mmapobj_ptr = find_mmapobj( vma_id, proc_obj );
	if(!mmapobj_ptr) {
		printf("nv_commit:finding mmapobj failed %d \n", vma_id);
		goto error;
	}
	mmapobj_ptr->length = rqst->bytes; 

#ifdef CHCKPT_HPC
	mmapobj_ptr->ops = ops;
	mmapobj_ptr->order_id = rqst->order_id;
#endif

	if(!src){
		printf( "nv_commit:dram src pointer is null \n");
		goto error;
	}

	//found the mmapobj ptr
	if (size <= 0 ) {
		printf( "nv_commit:very few bytes to copy \n");
		goto error;
	}

#ifdef NV_DEBUG
	fprintf(stdout, "nv_commit:getting mmapobj start address \n");
#endif

	//get the mmapobj starting address
	if ( !mmapobj_ptr ||  !(mmapobj_ptr->proc_obj)) {
		printf("nv_commit: could not locate process obj or mmapobj\n");
		goto error;
	}
   addr = rqst->mem;
   memcpy((void *) addr, src, size);
#ifdef NV_DEBUG
   printf("nv_commit: after spin lock %d \n",  
			mmapobj_ptr->proc_obj->pid); 
#endif
	return 0;

	error:
	return -1;
}


/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
/*----------Checkpoint chunk protection code--------------------*/
int enable_chunkprot(int chunkid) { 
	return enabl_chunkprot_using_map(chunkid);
}


size_t nv_disablprot(void *addr, int *curr_chunkid) {

	int chunkid = 0;
	size_t length = 0;

	//if(prev_proc_id == 1) {
        //fprintf(stdout,"fault address %lu\n",(ULONG)addr);
	//}

	length =remove_chunk_prot(addr, &chunkid);
	enabl_exclusv_chunkprot(chunkid);
	*curr_chunkid = chunkid;


#ifdef _USE_FAULT_PATTERNS

	int nxt_chunk=0;

	//if(find_chunk_fault_list(chunkid)){	
   if(!chunk_fault_lst_freeze)
		add_chunk_fault_lst(chunkid);
	
	else {
		/*get the next chunk to be protected
		set the protection of the chunk*/
		nxt_chunk = get_next_chunk(chunkid);

		if(!nxt_chunk)
			goto end;

		//if(prev_proc_id == 1)
		//fprintf(stderr,"Curr %d -> Next %d\n",
		//				chunkid, nxt_chunk);

		if(!nxt_chunk || enable_chunkprot(nxt_chunk))
			if(prev_proc_id == 1)
				fprintf(stderr,"chun protection failed\n");
	}
#endif
end:
	return length;
}


int set_chunkprot() { 
	return set_chunkprot_using_map();
}



int add_to_fault_lst(int id) {

	int val = 0;
	
	fault_chunk[id] = 0;
	val = fault_chunk[id];
	val++;
	fault_chunk[id] = val;	

	if(!stop_history_coll && (checpt_cnt > 0))
	  fault_hist[id]++;

}

int clear_fault_lst() {

    int faultid = 0;
    for( fault_itr= fault_chunk.begin();
                  fault_itr!=fault_chunk.end();
                  ++fault_itr){

		faultid = (*fault_itr).first; 
		fault_chunk[faultid]=0;
   }
}

int start_asyn_lcl_chkpt(int chunkid) {

	int faultid=0;
	int fault_cnt =0;

	//fprintf(stdout,"proc id %d \n",prev_proc_id);
	for( fault_itr= fault_chunk.begin(); 
			fault_itr!=fault_chunk.end(); 
			++fault_itr){
			
		faultid = (*fault_itr).first;			
		fault_cnt = (*fault_itr).second;

		//if(prev_proc_id == 1){
		// fprintf(stdout,"chunk id %d fault_hist[id] %d \n",faultid, fault_hist[faultid]);
		//}
		//copy_dirty_chunk(faultid);
 	    /*if(fault_hist[faultid]  &&(fault_cnt >= (fault_hist[faultid]))){
			copy_dirty_chunk(faultid, 1);
		}else{
			copy_dirty_chunk(faultid, 0);	
		}*/
		copy_dirty_chunk(faultid, 1);	

	}
	return 0;
}









