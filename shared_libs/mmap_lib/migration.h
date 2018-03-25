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
#include <time.h>


#ifdef __cplusplus
extern "C" {
#endif
int record_addr(void* addr, size_t size);
void migrate_pages(int node);
int migrate_now();
void init_allocs();
void stopmigrate();
#ifdef __cplusplus
}
#endif

#endif
