//#define _POSIX_C_SOURCE 200809L

#define _GNU_SOURCE
 #include <errno.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
//#include <numa.h>
#include <time.h>
#include <inttypes.h>
#include <pthread.h>

//#define _STOPMIGRATION //also in make file

#define BUGFIX

#define MAX_ENTRIES 100*1024*1024
#define OBJECT_TRACK_SZ 1024*64
#define MAXPAGELISTSZ 1024*1024*100
#define NODE_TO_MIGRATE 1
#define MIGRATEFREQ 5
//#define HINT_MIGRATION

#define __NR_move_inactpages 317
#define __NR_NValloc 316

static int init_alloc;
static unsigned int alloc_cnt;
static unsigned int g_allocidx, g_uselast_off;
unsigned long *chunk_addr = NULL;
size_t *chunk_sz = NULL;
int *chunk_mig_status=NULL;
static unsigned int offset;
struct timespec spec;
void **migpagelist = NULL;
static size_t stat_allocsz;

//int init = 0;
struct bitmask *old_nodes;
struct bitmask *new_nodes;
static int init_numa;

#ifdef HINT_MIGRATION
//flag to indicate if migration should start
static int migratenow_flag;
#endif


//pthread code
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t largemutex= PTHREAD_MUTEX_INITIALIZER;
pthread_t thr;

/******GLOBAL Functions*********/
void migrate_pages(int node);
/******GLOBAL Functions*********/


/*******************************************/
int migrate_now(){

#ifdef HINT_MIGRATION
	//fprintf(stdout,"setting hetero hint \n");
	migratenow_flag=1;
#endif
	return 0;

}
/*******************************************/




void *get_alloc_pagemap(unsigned int *count, size_t **ptr){

    *count = offset;
    *ptr= chunk_sz;
    return (void *)chunk_addr;
}


void call_migrate_func()
{
	while(1) {
		migrate_pages(NODE_TO_MIGRATE);    
	}
	return;
}


  #define handle_error_en(en, msg) \
		do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

   int  setaff(int aff)
   {
	   int s, j;
	   cpu_set_t cpuset;
	   pthread_t thread;

		j=aff;

	   thread = pthread_self();

	   /* Set affinity mask to include CPUs 0 to 7 */
	   CPU_ZERO(&cpuset);
	   CPU_SET(j, &cpuset);

	   s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	   if (s != 0)
		   handle_error_en(s, "pthread_setaffinity_np");

	   /* Check the actual affinity mask assigned to the thread */
	   s = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	   if (s != 0)
		   handle_error_en(s, "pthread_getaffinity_np");

	   printf("Set returned by pthread_getaffinity_np() contained:\n");
	   if (CPU_ISSET(j, &cpuset))
		   printf("    CPU %d\n", j);

	   //exit(EXIT_SUCCESS);
	   return 0;
   }





void * entry_point(void *arg)
{
    printf("starting thread\n");
	setaff(3);
    call_migrate_func();
    printf("exiting thread\n");
    return NULL;
}

void init_allocs() {

	//pthread_mutex_lock(&mutex);

    chunk_addr =(unsigned long *)mmap (0, sizeof(unsigned long) * MAX_ENTRIES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    chunk_sz = (size_t *)mmap (0, sizeof(size_t) * MAX_ENTRIES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    chunk_mig_status = (int *)mmap (0, sizeof(int) * MAX_ENTRIES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	migpagelist = (void **)mmap (0, MAXPAGELISTSZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if(!chunk_addr) {
		//pthread_mutex_unlock(&mutex);
	    assert(chunk_addr);
	}

	if(!chunk_sz) {
		//pthread_mutex_unlock(&mutex);
	    assert(chunk_sz);
	}

	if(!chunk_mig_status) {
		//pthread_mutex_unlock(&mutex);
		assert(chunk_mig_status);
	}

	if(pthread_create(&thr, NULL, &entry_point, NULL))
    {
        printf("Could not create thread\n");
		//pthread_mutex_unlock(&mutex);
        assert(0);
    }
	
	init_alloc = 1;

	//pthread_mutex_unlock(&mutex);
}

int record_addr(void* addr, size_t size) {

#ifdef _STOPMIGRATION
	return 0;
#endif

	if(size < OBJECT_TRACK_SZ)
		return -1;

    if(!init_alloc){
		pthread_mutex_lock(&largemutex);
        init_allocs();
		pthread_mutex_unlock(&largemutex);
    }

#if 1
	return 0;
#endif

#ifdef _DEBUG
	fprintf(stderr,"migration: Recording allocation %zu\n", size);
#endif
    if(size >= OBJECT_TRACK_SZ) {
        chunk_addr[offset]=(unsigned long)addr;
        chunk_sz[offset]=size;
		chunk_mig_status[offset]=0;
        offset++;
        alloc_cnt++;
		stat_allocsz += size;
    }

    return 0;
}



#if 1

unsigned int migcntidx=0;

void clear_migrated_pages(int *status, unsigned int migcnt){

	unsigned int idx =0;
	
	for(idx=0; idx < migcnt; idx++) {
		if(status[idx] == 1) {
			chunk_mig_status[idx]=1;
			migpagelist[idx] = 0;
			migcntidx++;
		}else {
			//printf("clear_migration: page off %u ret code %d \n",
			//		idx, status[idx]); 
			//g_uselast_off++;
		}
	}
//#ifdef _DEBUG
	fprintf(stderr,"total migrated pages %u "
			"from total alloc size %u "
			"and total pages %u \n", 
			migcntidx, stat_allocsz, stat_allocsz/4096);
//#endif
}

void **get_pages(unsigned long *alloc_arr, size_t *sizearr, 
				unsigned int alloc_cnts , unsigned int *migcnt){

	int cnt=0, i=0, page_count=0;
	long pagesize=4096;
	char *pages=NULL;
	void *page_base = NULL;
	size_t allocsz=0;
	long local_off = 0;
	unsigned int alloc_idx=0;

	alloc_idx = g_allocidx;
			
	for(cnt = alloc_idx; cnt < alloc_cnts; cnt++){

		allocsz = sizearr[cnt];
		page_count = allocsz/pagesize; 
		page_base = (void *)alloc_arr[cnt];

		pages = (char *) ((((long)page_base) & ~((long)(pagesize - 1))) + pagesize);

		//printf("page_count %u, offset %u\n",page_count, offset);
		for (i = 0; i < page_count; i++) {

			//check for free slots
			if(migpagelist[local_off]==0){
			    //pages[ i * pagesize ] = (char) i;
			 	migpagelist[local_off] = pages + i * pagesize;
			  	local_off++;
			}else {
				if(i) i = i-1;
				local_off++;
			}
		}
		g_uselast_off = local_off;
	}

	//look for remaining
	if(local_off == 0){

		unsigned int tmp_pgcnt =0;
		long setpos = -1;

		for (local_off =0; local_off < g_uselast_off; local_off++ ) {

			//if already migrated, set position
			if(migpagelist[local_off]==0 && setpos < 1){
				setpos = local_off;
			}
			else if(migpagelist[local_off]==0 && setpos > 0){
				continue;
			}
			else if(setpos != -1){
				/*swap */
				char *tmp = (char *)migpagelist[local_off];
				//tmp[ i * pagesize ] = (char) i;
				migpagelist[setpos] = (char *) ((((long)tmp) & ~((long)(pagesize - 1))) + pagesize);
				migpagelist[local_off]=0;
				tmp_pgcnt++;
				setpos++;
			}else {
				tmp_pgcnt++;
			}
		}
		g_uselast_off = tmp_pgcnt;
	}

	alloc_idx = cnt;
#ifdef _DEBUG
	fprintf(stderr,"offset %ld, alloc_idx %u, alloc_cnts %u "
					"g_uselast_off %u \n",
					local_off, alloc_idx, alloc_cnts, g_uselast_off);
#endif

	g_allocidx = alloc_idx;

	*migcnt = g_uselast_off;

	return migpagelist;
}


int migrate_fn() {

	int nr_nodes=0, rc=-1;	
	int cnt=0;
	int migret=0;	

   if(!init_numa) {
        //numa_init();
        //nr_nodes = numa_max_node()+1;
        //printf("print migrate pages %d\n",nr_nodes);
        //old_nodes = numa_bitmask_alloc(nr_nodes);
        //new_nodes = numa_bitmask_alloc(nr_nodes);
        //numa_bitmask_setbit(old_nodes, 0);
        //numa_bitmask_setbit(new_nodes, 1);
        init_numa = 1;
    }
	//printf("Migrating pages\n");

#ifndef _STOPMIGRATION

#ifdef HINT_MIGRATION
	if(!migratenow_flag){
		return 0;
	}else {
		 migratenow_flag=0;
	}
#endif	

	fprintf(stdout,"migrating now \n");
	/*rc = numa_migrate_pages(0, old_nodes, new_nodes);
    if (rc < 0) {	
        perror("numa_migrate_pages failed");
		exit(-1);
    }*/

  	/*for(cnt=0; cnt < alloc_cnt; cnt++) {

		//fprintf(stdout,"calling migrate system call function \n");
		migret =(unsigned long) syscall(__NR_move_inactpages,
								(unsigned long)chunk_addr[cnt],
								(unsigned long)chunk_sz[cnt]);

		//fprintf(stdout,"total migrated pages %d\n",migret);
		//if(migret){ 
			//(char *) syscall(__NR_NValloc, 10);		
		//}
	  }*/

	//migret =(unsigned long) syscall(__NR_move_inactpages,
	//							(unsigned long)999999999999,
	//							(unsigned long)100);

#endif
	return 0;

}


void migrate_pages(int node) {

	unsigned int pgcount=0, migcnt=0;
	int *status;
	int *nodes;
	int i =0, rc=0;
	struct timespec currspec;
	int sec;
	int nr_nodes = 2; //numa_max_node()+1;
	size_t *sizearr = NULL;


	clock_gettime(CLOCK_REALTIME, &currspec);

	if((currspec.tv_sec - spec.tv_sec) < MIGRATEFREQ)
		goto gettime; 

#ifndef BUGFIX
	void *memptr=NULL;

	//pthread_mutex_lock(&mutex);
	memptr = get_alloc_pagemap(&pgcount, &sizearr);
	if(!pgcount) {
		//assert(pgcount);
		fprintf(stderr,"No pages yet... \n");
		return; 
	}

	get_pages((unsigned long*)memptr, sizearr,  pgcount, &migcnt);

#ifdef _DEBUG
	printf("migcnt %d \n",migcnt);
#endif

	pgcount = migcnt;

	if(!pgcount)
		 goto gettime;

	status =(int *)mmap (0, sizeof(int) * pgcount, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	nodes =	(int *)mmap (0, sizeof(int) * pgcount, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	assert(migpagelist);
	assert(status);
	assert(nodes);

	node = 1;
	for (i = 0; i < pgcount; i++) {
         nodes[i] = node;
         status[i] = -123;
     }
#endif

	//fprintf(stdout,"migrate_pages(), pagecount %u\n", pgcount);

	 /* Move to node zero */
#ifndef BUGFIX
	numa_move_pages(0, migcnt, migpagelist, nodes, status, 0);
	//pages already migrated to be removed from list
	clear_migrated_pages(status, migcnt);
	munmap(status,sizeof(int) * pgcount);
	munmap(nodes,sizeof(int) * pgcount);
	//munmap(migpagelist,MAXPAGELISTSZ);
	clock_gettime(CLOCK_REALTIME, &spec);
	//pthread_mutex_unlock(&mutex);

#else

	migrate_fn();
	clock_gettime(CLOCK_REALTIME, &spec);

#endif
	return;

gettime:
	//clock_gettime(CLOCK_REALTIME, &spec);
	//pthread_mutex_unlock(&mutex);
	return;



#if 0
	clock_gettime(CLOCK_REALTIME, &currspec);

	if((currspec.tv_sec - spec.tv_sec) < 1)
		goto gettime;

	addr = MmapWrapper::get_app_page_list(&pgcount);
	assert(pgcount);

	pthread_mutex_lock(&mutex);

	status =(int *)mmap (0, sizeof(int) * pgcount, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	nodes =	(int *)mmap (0, sizeof(int) * pgcount, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(status);
	assert(nodes);
	for (i = 0; i < pgcount; i++) {
            nodes[i] = node;
            status[i] = -123;
     }

	//fprintf(stdout,"migrate_off %u pgcount %u\n", migrate_off,pgcount);

	 /* Move to node zero */
	numa_move_pages(0, pgcount, addr, nodes, status, 0);
	//fprintf(stdout,"migrated pages %u \n", migrate_off);
	clock_gettime(CLOCK_REALTIME, &spec);
	migrate_off += pgcount;
	munmap(status,sizeof(int) * pgcount);
	munmap(nodes,sizeof(int) * pgcount);
	pthread_mutex_unlock(&mutex);

gettime:
	;
#endif
}
#endif


