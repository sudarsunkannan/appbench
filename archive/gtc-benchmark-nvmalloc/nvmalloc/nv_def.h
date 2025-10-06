
#ifndef NV_DEF_H_
#define NV_DEF_H_



 #define SHMEM_ID 2078
 #define PAGE_SIZE 4096

//This denotes the size of persistem memory mapping
// for each process. Note, the metadata mapping is a seperate
//memory mapped file for each process currently
#define METADAT_SZ 1024 * 1024 * 1024


#define PROT_NV_RW  PROT_READ|PROT_WRITE
#define PROT_NV_RDONLY  PROT_READ


#define PROT_ANON_PRIV MAP_PRIVATE | MAP_ANONYMOUS

//base name of memory mapped files
#define FILEPATH "/tmp/chkpt"
#define MAPMETADATA_PATH "/tmp/chkmeta"

#define  SUCCESS 0
#define  FAILURE -1

//Page size
#define SHMSZ  4096 


//NVRAM changes
#define NUMINTS  (10)
#define FILESIZE (NUMINTS * sizeof(int))
#define __NR_nv_mmap_pgoff     301
#define __NR_mmap 192

//FIXME: UNUSED FLAG REMOVE
#define MMAP_FLAGS MAP_PRIVATE

//Enable debug mode
//#define NV_DEBUG

//Enable locking
#define ENABLE_LOCK 1

#define MAX_DATA_SIZE 1024 * 1024 *1524

#define NVRAM_DATASZ 1024 * 4

//Maximum number of process this library
//supports. If you want more proecess
//increment the count
#define MAX_PROCESS 64

//Random value generator range
//for temp nvmalloc allocation
#define RANDOM_VAL 1433
//#define NVRAM_OPTIMIZE


#define BASE_METADATA_NVID 6999
#define BASE_METADATA_SZ 1024*1024

#define CHKSUM_LEN 40 

#define ENABLE_CHECKPOINT

//#define VALIDATE_CHKSM

#define ENABLE_MPI_RANKS

#endif
