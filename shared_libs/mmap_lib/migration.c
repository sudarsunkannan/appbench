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

#define BUGFIX
#define MAX_ENTRIES 100*1024*1024
#define OBJECT_TRACK_SZ 1024*64
#define MAXPAGELISTSZ 1024*1024*100
#define NODE_TO_MIGRATE 1
#define SLEEPTIME 100000
#define THREADAFF 0
#define HOT_MIN_MIG_LIMIT 0
#define STOPCOUNT 100000
#define HETERO_APP_INIT 10
#define MIGRATEFREQ 100000
#define LLC 1000000
#define MAXHOTPAGE 1000000
//#define HINT_MIGRATION
//
#define XEN_HOTSCAN_FREQ 100
#define XEN_HOTSCAN_FREQ 100
#define USE_SHARED_MEM 1
#define MAX_HOT_SCAN 2048

#define __NR_move_inactpages 317
#define __NR_NValloc 316

int init_alloc;
static unsigned int alloc_cnt;
static unsigned int g_allocidx, g_uselast_off;
unsigned long *chunk_addr = NULL;
size_t *chunk_sz = NULL;
int *chunk_mig_status=NULL;
static unsigned int offset;
struct timespec spec;
void **migpagelist = NULL;
static size_t stat_allocsz;
unsigned int stopmigcnt=0;

//int init = 0;
struct bitmask *old_nodes;
struct bitmask *new_nodes;
static int init_numa;
static int stopmigration;

#ifdef HINT_MIGRATION
//flag to indicate if migration should start
static int migratenow_flag;
#endif

static void con() __attribute__((constructor)); 
//pthread code
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t largemutex= PTHREAD_MUTEX_INITIALIZER;
pthread_t thr;

/******GLOBAL Functions*********/
void migrate_pages(int node);
/******GLOBAL Functions*********/

void con() {
    //fprintf(stderr,"I'm a constructor\n");
	init_allocs();
    
}
/*******************************************/



void call_migrate_func()
{
	migrate_pages(NODE_TO_MIGRATE);    
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

	   return 0;
   }



void * entry_point(void *arg)
{
    printf("starting thread\n");
	setaff(THREADAFF);
    call_migrate_func();
    printf("exiting thread\n");
    return NULL;
}

void init_allocs() {

#ifdef _STOPONDEMAND
	return;
#endif
	if(init_alloc) {
		return;
	}else {
	fprintf(stderr,"calling init_alloc \n");
	(unsigned long) syscall(__NR_move_inactpages,(unsigned long)LLC,(unsigned long)HETERO_APP_INIT, XEN_HOTSCAN_FREQ, MAX_HOT_SCAN, MAXHOTPAGE, USE_SHARED_MEM, 10);
	}
	init_alloc = 1;
#ifdef _STOPMIGRATION
	return 0;
#endif
	if(pthread_create(&thr, NULL, &entry_point, NULL))
    {
        printf("Could not create thread\n");
        assert(0);
    }
}


int migrate_fn() {

	int nr_nodes=0, rc=-1;	
	int cnt=0;
	int migret=0;	


#ifndef _STOPMIGRATION

#ifdef HINT_MIGRATION
	if(!migratenow_flag){
		return 0;
	}else {
		 migratenow_flag=0;
	}
#endif	
	migret =(unsigned long) syscall(__NR_move_inactpages,
								(unsigned long)999999999999,
								(unsigned long)100, 
								XEN_HOTSCAN_FREQ, MAX_HOT_SCAN, MAXHOTPAGE, USE_SHARED_MEM, 10);
	if(migret < HOT_MIN_MIG_LIMIT) {
		usleep(SLEEPTIME);	
	}
#endif
	return 0;

}

void migrate_pages(int node) {

	struct timespec currspec;
	int sec;
	long double doublediff=0;

	while(1) {
		if( stopmigcnt < STOPCOUNT){
			stopmigcnt++;	
		}else {
			stopmigration = 1;
		}

		if(stopmigration){
			sleep(MIGRATEFREQ);
			return 0;
		}
		migrate_fn();
		usleep(MIGRATEFREQ);
	}
		return;
}


