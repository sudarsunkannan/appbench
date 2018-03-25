#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include "mmaplib.h"

 int  setup_map_file(char *filepath, unsigned long bytes)
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


void* _mmap_wrap(void *addr, size_t size, int mode, int prot, int fd, int offset){

    void *ret = NULL;

    ret = mmap(addr, size, mode , prot, fd, offset);
	assert(ret != MAP_FAILED);

    return ret;
}


void* _mmap_write(char *fname, size_t bytes){

    void *ret = NULL;
	int fd = -1;

	fd = setup_map_file(fname, bytes);

	assert(fd >= 0);

	ret = _mmap_wrap(0,  bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ret == MAP_FAILED) {
        fprintf(stderr, "mmap failed \n");
        close(fd);
        exit(-1);
   }
    return ret;
}

void* _mmap_read(char *fname, size_t bytes){

    void *ret = NULL;
	int fd = -1;
	FILE *fp = NULL;

	fp = fopen(fname, "a+");
	assert(fp);
	fd = fileno(fp);

	ret = _mmap_wrap(0,  bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ret == MAP_FAILED) {
        fprintf(stderr, "mmap failed \n");
        close(fd);
        exit(-1);
   }
    return ret;
}


int _munmap(void *addr, size_t size){

    int ret_val = 0;

    if(!addr) {
        perror("null address \n");
        return -1;
    }
   ret_val = munmap(addr, size);
   return ret_val;
}





