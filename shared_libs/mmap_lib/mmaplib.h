#ifndef MMAPLIB_H_
#define MMAPLIB_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>


#ifdef __cplusplus
extern "C" {
#endif

int  setup_map_file(char *filepath, unsigned long bytes);
void* _mmap_wrap(void *addr, size_t size, int mode, int prot, int fd, int offset);
void* _mmap_write(char *fname, size_t bytes);
void* _mmap_read(char *fname, size_t bytes);
int _munmap(void *addr, size_t size);


#ifdef __cplusplus
}
#endif

#endif
