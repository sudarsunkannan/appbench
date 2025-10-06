#include "checkpoint.h"
#include <iostream>
#include <string>
#include <map>
#include <signal.h>
#include <queue>
#include <list>
#include "nv_def.h"
#include "util_func.h"


#include <iostream>
#include <string>
#include <map>
#include <algorithm>
#include <functional>

#include <pthread.h>

using namespace std;

std::map <int, size_t> proc_vmas;
std::map <int, size_t> metadata_vmas;
std::map<int, size_t>::iterator vma_itr;
std::map <void *, chunkobj_s *> chunkmap;
std::map <int, chunkobj_s *> id_chunk_map;
std::map <void *, chunkobj_s *>::iterator chunk_itr;
std::queue<int> sig_queue;

std::list<int> chunk_fault_list;


/*Lock globals*/
cktptlock_s *g_chkptlock;
static sigset_t newmask;
extern unsigned int prev_proc_id;



static void sig_remote_chkpt(int signo);

int record_chunks(void* addr, chunkobj_s *chunk) {

	chunkmap[addr] = chunk;
	id_chunk_map[chunk->chunkid] = chunk;
	if(prev_proc_id == 1)
	fprintf(stderr,"recording %ld \n",(unsigned long)addr);
	return 0;
}

int get_chnk_cnt_frm_map() {

	return chunkmap.size();
}

void* get_chunk_from_map(void *addr) {

	chunkobj_s *chunk;
	size_t bytes = 0;
	unsigned long ptr = (unsigned long)addr;
    unsigned long start, end;
	
    for( chunk_itr= chunkmap.begin(); chunk_itr!=chunkmap.end(); ++chunk_itr){
        chunk = (chunkobj_s *)(*chunk_itr).second;
        bytes = chunk->length;
		start = (ULONG)(*chunk_itr).first;
		end = start + bytes;
#ifdef NV_DEBUG
		fprintf(stderr,"fetching %ld start %ld end %ld\n",ptr, start, end);
#endif
		if( ptr >= start && ptr <= end) {
			return (void *)chunk;
		}
	}
	return NULL;
}



int record_vmas(int vmaid, size_t size) {

    proc_vmas[vmaid] = size;
    return 0;
}

int record_metadata_vma(int vmaid, size_t size) {

    metadata_vmas[vmaid] = size;
    return 0;
}


size_t get_vma_size(int vmaid){

	vma_itr=proc_vmas.find(vmaid); 
	if(vma_itr == proc_vmas.end())
		return 0;
	return proc_vmas[vmaid];
}


int get_vma_dirty_pgcnt(int procid, int vmaid) {

	UINT numpages;
	struct nvmap_arg_struct a;
	size_t bytes =0;
	void *dirtypgbuff = NULL;
    UINT offset = 0;

	a.fd = -1;
    a.offset = offset;
    a.vma_id =vmaid;
    a.proc_id =procid;
    a.pflags = 1;
    a.noPersist = 0;

	bytes = INTERGER_BUFF * sizeof(unsigned int);
	dirtypgbuff =   malloc(bytes);
	dirtypgbuff = dirtypgbuff + PAGE_SIZE;
	numpages =syscall(__NR_copydirtpages, &a, dirtypgbuff);
    fprintf(stdout, "Get dirty pages %d \n",numpages);

	return numpages;
}

int copy_dirty_pages(int procid, int vmaid, void *buffer, int bytes)
{
    void *map;
    UINT offset = 0;
    struct nvmap_arg_struct a;

    a.fd = -1;
    a.offset = offset;
    a.vma_id =vmaid;
    a.proc_id =procid;
    a.pflags = 1;
    a.noPersist = 0;
    
    if(bytes){
        map= (void *) syscall(__NR_nv_mmap_pgoff, 0, bytes,
                     PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS , &a );
		//map= (void *) mmap(0, bytes,PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS,-1, 0);
        if (map == MAP_FAILED) {
            perror("Error mmapping the file");
            exit(EXIT_FAILURE);
        }
        memcpy(buffer, map, bytes);
    }
    return 0;
}


void *copy_header(void *buff, int procid, int storeid, 
		size_t bytes, int type){

	chkpthead_s ckptstruct;

	ckptstruct.pid = procid;
	ckptstruct.type = type;
    ckptstruct.storeid = storeid;
	ckptstruct.bytes = bytes;

	assert(buff);
	memcpy(buff, &ckptstruct, sizeof(chkpthead_s));

	return buff;
}


void* helper_update_sendbuff(void *sendbuff, 
						void *tmp,
						size_t bytes,
						size_t *sent_bytes,
						int procid,
						int type,
						int vmaid) {

	size_t header_sz =0;
	header_sz = sizeof(chkpthead_s);

	tmp = sendbuff;
	tmp = tmp + *sent_bytes;
	copy_header(tmp, procid, vmaid,bytes, type);
	tmp = tmp + header_sz;
	return tmp;
}


void* copy_proc_checkpoint(int procid, size_t *chkpt_sz,
							 int check_dirtpages ) {

    int numpages = 0;
    size_t bytes = 0, total_size=0, sent_bytes =0;
	size_t header_sz =0;
    void *sendbuff = 0, *tmp = NULL;
    int type =0, vmaid =0, mapid = 0;

    if(!proc_vmas.size()){
        perror("no vmas added yet \n");
        return NULL;
    }

	header_sz = sizeof(chkpthead_s);

   /*for( vma_itr= metadata_vmas.begin(); vma_itr!= metadata_vmas.end(); ++vma_itr){

		vmaid = (*vma_itr).first;
		bytes = (*vma_itr).second;

		total_size = total_size + bytes + header_sz;
		sendbuff = realloc(sendbuff,total_size);
		assert(sendbuff);

		tmp = helper_update_sendbuff(sendbuff,
							tmp,bytes, &sent_bytes,
							procid, type, vmaid);

       copy_dirty_pages(procid,vmaid,tmp, bytes);
	   sent_bytes =  sent_bytes + bytes + header_sz;	

	}*/

    for( chunk_itr= chunkmap.begin(); chunk_itr!=chunkmap.end(); ++chunk_itr){
		//vmaid = (*chunk_itr).first;
        chunkobj_s *chunk = (chunkobj_s *)(*chunk_itr).second;
        bytes = chunk->length;  
		mapid = chunk->vma_id;
		vmaid = chunk->chunkid;

        assert(bytes);
   	    if(!bytes)
       	    continue;

		 chunk->chunk_commit = 1;

        total_size = total_size + bytes + header_sz;
        sendbuff = realloc(sendbuff,total_size);
        assert(sendbuff);

		tmp = helper_update_sendbuff(sendbuff,
							tmp,bytes, &sent_bytes,
							procid, type, vmaid);

       //fprintf(stdout, "procid %d proc_vmas[0] %d \n",
		//				procid, mapid);    
       copy_dirty_pages(procid,mapid,tmp, bytes);
		//assert(chunk->nv_ptr);
		//memcpy(tmp, chunk->nv_ptr, bytes);
	    sent_bytes =  sent_bytes + bytes + header_sz;	
    }

	//vma_itr= proc_vmas.begin();
    //int mapid = (*vma_itr).first;
   /*for( vma_itr= proc_vmas.begin(); vma_itr!=proc_vmas.end(); ++vma_itr){

        vmaid = (*vma_itr).first;
        if(check_dirtpages) {

            numpages = get_vma_dirty_pgcnt(procid, vmaid);
            bytes =numpages * PAGE_SIZE;
            fprintf(stdout, "procid: %d vmaid: %d, "
                    "dirtypages %d header_sz %u\n",
                    procid, vmaid,numpages, header_sz);
        }else{

            bytes = (*vma_itr).second;
        }
        assert(bytes);
   	    if(!bytes)
       	    continue;

        total_size = total_size + bytes + header_sz;
        sendbuff = realloc(sendbuff,total_size);
        assert(sendbuff);

		tmp = helper_update_sendbuff(sendbuff,
							tmp,bytes, &sent_bytes,
							procid, type, vmaid);

       copy_dirty_pages(procid,vmaid,tmp, bytes);
	   sent_bytes =  sent_bytes + bytes + header_sz;	
    }*/
    *chkpt_sz = total_size;
	//fprintf(stdout, "total size %d sendbuff %ld \n", total_size, (unsigned long)sendbuff);

	//parse_data(sendbuff, total_size);
	return sendbuff;
}


void print_header( chkpthead_s *header) {

	assert(header);

	fprintf(stdout, "header->pid %d \n",header->pid);
	fprintf(stdout, "header->type %d \n",header->type);
	fprintf(stdout, "header->storeid %d \n",header->storeid);
	fprintf(stdout, "header->bytes %d \n",header->bytes);

}


int add_nvram_data(chkpthead_s *header) {

	rqst_s rqst;
	void *ret =0;

    rqst.pid = header->pid;
	rqst.id = header->storeid;
	rqst.bytes = header->bytes;
    assert(rqst.id);
	ret = map_nvram_state(&rqst);

#ifdef _DEBUG
	fprintf(stdout,"finshed mapping nvram state \n");
#endif

	assert(ret);
	return 0;
}


int parse_data(void *buffer, size_t size) {


	size_t header_sz =0;
	void *tmpbuff = NULL;
	chkpthead_s header, tmpheader;
    size_t itr =0;

	assert(buffer);
	header_sz = sizeof(chkpthead_s);

	while (itr < size) {

		memcpy((void *)&tmpheader, buffer, header_sz);

#ifdef _DEBUG
		print_header(&tmpheader);
#endif

		add_nvram_data(&tmpheader);

		buffer = buffer + tmpheader.bytes + header_sz;

		itr = itr + tmpheader.bytes + header_sz;
	}

	return 0;
}



void* get_shm(int id, int flag) {

        key_t key;
        int shmid;
        void *shm;
    
        key = id + 10000;

        if ((shmid = shmget(key,SHMSZ, flag)) < 0) {
			fprintf(stdout, "getting shm %d \n", key);
			//perror("shmget");
            return NULL;
        }

        if ((shm = shmat(shmid, (void *)0, 0)) == (void *)-1) {
    	    //perror("shmat");
            return NULL;
        }

        return shm;
}
 

cktptlock_s *create_shm_lock(int id) {

	void *shm;
	cktptlock_s *lock;
	int flag = 0;

	flag = 0666 | IPC_CREAT;

	shm = get_shm(id,flag);
	assert(shm);

	lock = (cktptlock_s *)shm;

	lock->dirty = 0;	

	return lock;
}

cktptlock_s *get_shm_lock(int id) {

	void *shm;
	cktptlock_s *lock;
	int flag = 0;

	flag = 0666;


	shm = get_shm(id,flag);
	if(!shm)
		return NULL;

	lock = (cktptlock_s *)shm;
	
	return lock;
}



int init_shm_lock(int id) {

    if(g_chkptlock)
		return 0;

	g_chkptlock = create_shm_lock(id);
	g_chkptlock->siglist = -1;
	assert(g_chkptlock);
	gt_spinlock_init(&g_chkptlock->lock);
	return 0;
}

int set_acquire_chkpt_lock(int id) {

	if(!g_chkptlock) {
		return -1;
	}

	 gt_spin_lock(&g_chkptlock->lock);

	return 0;
}




int send_lock_avbl_sig(int signo) {

	if(g_chkptlock->siglist >= 0){
#ifdef NV_DEBUG
		fprintf(stdout,"successfuly sent signal"
				" %d\n", g_chkptlock->siglist);	
#endif
	 	kill(g_chkptlock->siglist,signo);
	}
	else{
		fprintf(stdout,"send signal failed\n");
		return -1;
	}
}



/*SIGNAL handling functions */
int register_ckpt_lock_sig(int pid, int signo) {


    g_chkptlock = get_shm_lock(pid);

	if(!g_chkptlock)
		return -1;

	g_chkptlock->siglist = getpid();

#ifdef NV_DEBUG
	fprintf(stderr,"registering for signal %d \n", g_chkptlock->siglist);
#endif

	/*now register for signal*/
	if (signal(signo, sig_remote_chkpt) == SIG_ERR){
		 printf("signal(SIGINT) error\n");
		 exit(1);
	 }

	return 0;
}

int wait_for_chkpt_sig(int pid, int signo) {

   volatile int flg =0;	

	sigemptyset(&newmask);
	sigaddset(&newmask, signo);
	sigprocmask(SIG_BLOCK, &newmask, NULL);
#ifdef NV_DEBUG
	 fprintf(stdout,"waiting for signal %d,"
			 "mypid %d\n", signo, getpid());
#endif
	sigwait(&newmask, &signo);
#ifdef NV_DEBUG
	 fprintf(stdout,"got signal\n");
#endif


	if(!g_chkptlock)
    g_chkptlock = get_shm_lock(pid);

    if(!g_chkptlock)
        return -1;

	while(!flg)	{
		flg = g_chkptlock->dirty;
#ifdef NV_DEBUG
	   fprintf(stdout,"waiting for dirty bit set \n");
#endif
	}
#ifdef NV_DEBUG
    fprintf(stdout,"dirty bit set \n");
#endif

	return 0;
}

int acquire_chkpt_lock(int id) {

	if(!g_chkptlock){
		return -1;
	}

	gt_spin_lock(&g_chkptlock->lock);

	return 0;
}


int disable_chkpt_lock(int id) {

    if(g_chkptlock);
    gt_spin_unlock(&g_chkptlock->lock);

}

int set_ckptdirtflg(int id) {

	assert(g_chkptlock);
	g_chkptlock->dirty = 1;
#ifdef NV_DEBUG
	fprintf(stderr,"g_chkptlock->siglist %d \n",
			g_chkptlock->siglist);
#endif
}

int disable_ckptdirtflag(int id) {

    assert(g_chkptlock);
    g_chkptlock->dirty = 0;

}

int get_dirtyflag( int id) {

	volatile int flg =0;

	if(!g_chkptlock) {
		fprintf(stdout,"shared mem not created %d\n",
				id);
		return 0;
	}

#ifdef NV_DEBUG
	fprintf(stdout,"get_dirtyflag->dirty %d\n",
			g_chkptlock->dirty);
#endif

	flg = g_chkptlock->dirty;

	return flg;
}

static void sig_remote_chkpt(int signo)
{

	//sig_queue.push(signo);
#ifdef NV_DEBUG
	fprintf(stdout, "remote signal recived %d\n", signo);
#endif
   // sigflag = 1;
	/*now register for signal*/
	if (signal(signo, sig_remote_chkpt) == SIG_ERR){
		 printf("signal(SIGINT) error\n");
		 exit(1);
	}	 
    return;
}


#ifdef _USE_FAULT_PATTERNS

int add_chunk_fault_lst(int chunkid){

	chunk_fault_list.push_back(chunkid);
	return 0;
}


int find_chunk_fault_list(int chunkid) {


	std::list<int>::iterator it;

	it = std::find(chunk_fault_list.begin(),chunk_fault_list.end(), chunkid);
	if(it == chunk_fault_list.end())
		return -1;
	else 
		return 0;

}

int find_nxtchunk_faultlst(int chunkid) {


	std::list<int>::iterator it;

    	it = std::find(chunk_fault_list.begin(),chunk_fault_list.end(), chunkid);
	it++;

	if(it == chunk_fault_list.end())
		return 0;
	else 
		return *it;

}



int get_next_chunk(int chunkid) {


	//return find_nxtchunk_faultlst(chunkid);
	int id =0;
	if(!chunk_fault_list.size())
		return -1;
	id = chunk_fault_list.front();
	chunk_fault_list.pop_front();
	chunk_fault_list.push_back(id);
	return id;
}

int check_chunk_fault_lst_empty() {

	if(chunk_fault_list.empty())
		return 1;
	else
		return 0;

}

int print_chunk_fault_lst() {

	std::list<int>::iterator itr;
	

	fprintf(stdout,"\n\n\n\n");	


	for(itr=chunk_fault_list.begin(); 
			itr != chunk_fault_list.end(); 
			itr++) {

		fprintf(stdout,"%d->",*itr);	
	}
	fprintf(stdout,"\n\n\n\n");	

	return 0;
}

#endif

int set_protection(void *addr, size_t len, int flag){

    
    if (mprotect(addr,len, flag)==-1) {
	if(prev_proc_id == 1)
	fprintf(stdout,"%lu \n", (unsigned long)addr);
        perror("mprotect");
        return EXIT_FAILURE;
    }
	return 0;
}

size_t remove_chunk_prot(void *addr, int *chunkid) {

	chunkobj_s *chunk = NULL;
	chunk =(chunkobj_s *)get_chunk_from_map(addr); 

	if(!chunk)
		fprintf(stdout, "chunk error %lu proc %d\n",
			(ULONG)addr, prev_proc_id);

	assert(chunk);
	chunk->chunk_commit = 0;
	chunk->dirty = 1; 
	*chunkid = chunk->chunkid;

	//if(blablabla)
#ifdef _USE_FAULT_PATTERNS
//	add_chunk_fault_lst(chunk->chunkid);
#endif
	
	//if(prev_proc_id == 1)
	//fprintf(stdout,"chunk:%d--->",chunk->chunkid);
	//fprintf(stdout,"fault chunk:%d \n",chunk->chunkid);


	set_protection(chunk->dram_ptr,chunk->dram_sz, 
					PROT_READ|PROT_WRITE);

	return chunk->length;
}

int set_chunkprot_using_map() {

	chunkobj_s *chunk;

	if(!chunkmap.size())
		return -1;

    for( chunk_itr= chunkmap.begin(); 
			chunk_itr!=chunkmap.end(); 
			++chunk_itr){

        chunk = (chunkobj_s *)(*chunk_itr).second;
		assert(chunk);

	    /*if(chunk->length < 1000000){
		 chunk->dirty = 1;	
         continue;
		}*/

		//if(prev_proc_id == 1)
		//	fprintf(stdout,"chunkid %d\n",chunk->chunkid);
		//if(!chunk->chunk_commit)
	   	set_protection(chunk->dram_ptr,
                    chunk->dram_sz,
                    PROT_READ);
    }
    return 0;
}



int enabl_chunkprot_using_map(int chunkid) {

	 chunkobj_s *chunk;

    chunk = (chunkobj_s *)id_chunk_map[chunkid];
    assert(chunk);

    /*if(chunk->length < 1024 * 1024) {
      chunk->dirty = 1;
      return 0;
    }*/
    assert(chunk->nv_ptr);
    assert(chunk->dram_ptr);

    set_protection(chunk->dram_ptr,chunk->dram_sz,PROT_READ);

	return 0;

}

int enabl_exclusv_chunkprot(int chunkid) {

	chunkobj_s *chunk;

	if(!chunkmap.size())
		return -1;

    for( chunk_itr= chunkmap.begin(); 
			chunk_itr!=chunkmap.end(); 
			++chunk_itr){

        chunk = (chunkobj_s *)(*chunk_itr).second;
		assert(chunk);
		if(chunk->chunkid ==chunkid)
			continue;

		if(chunk->dirty)
			 continue;

		
        /*if(chunk->length < 1000000){
	         chunk->dirty = 1;  
    	     continue;
        } */   

		assert(chunk->nv_ptr);
		assert(chunk->dram_ptr);
		//if(prev_proc_id == 1)
		//fprintf(stdout,"protected address %lu %d\n",(ULONG)chunk->dram_ptr, chunk->chunkid);
		set_protection(chunk->dram_ptr,
                    chunk->dram_sz,
                    PROT_READ);
		//chunk->chunk_commit = 0;
    }
    return 0;
}





int copy_dirty_chunks() {

	chunkobj_s *chunk;

    for( chunk_itr= chunkmap.begin(); 
			chunk_itr!=chunkmap.end(); 
			++chunk_itr){

        chunk = (chunkobj_s *)(*chunk_itr).second;
		assert(chunk);

		if(!chunk->dirty)
			continue;

		/*if(chunk->length < 1000000)
			continue;*/

		assert(chunk->nv_ptr);
		assert(chunk->dram_ptr);
		chunk->dirty = 0;
		memcpy_delay(chunk->nv_ptr,chunk->dram_ptr, chunk->length);

		//if(prev_proc_id == 1)
		//fprintf(stdout,"copied chunk %d \n", chunk->chunkid);
		//if(prev_proc_id == 1)
		//fprintf(stdout,"protected address %lu %d\n",(ULONG)chunk->dram_ptr, chunk->chunkid);
		set_protection(chunk->dram_ptr,chunk->dram_sz,PROT_READ);
		//fprintf(stdout,"copied chunk %d \n",chunk->chunkid);
    }

	//if(prev_proc_id == 1)
	//fprintf(stdout,"finished copying chunks \n");

    return 0;
}






int copy_dirty_chunk(int chunkid, int memcpy_flg) {

	chunkobj_s *chunk;

	chunk = (chunkobj_s *)id_chunk_map[chunkid];
	//if(!chunk)
	//	return -1;
	assert(chunk);

	if(!chunk->dirty)
		return 0;	

	/*if(chunk->length < 1000000) {
	  chunk->dirty = 1;
	  return 0;
	}*/

	if(memcpy_flg) {
		assert(chunk->nv_ptr);
		assert(chunk->dram_ptr);
		chunk->dirty = 0;
		memcpy_delay(chunk->nv_ptr,chunk->dram_ptr, chunk->length);
	}else {

	  chunk->dirty = 0;	
	}
	//if(prev_proc_id == 3)
	//fprintf(stdout,"copied chunk %d %d\n", chunk->chunkid, prev_proc_id);
	set_protection(chunk->dram_ptr,chunk->dram_sz,PROT_READ);
	
    return 0;
}







