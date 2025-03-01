#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int write_io_( float *f, int *elements, int *num_proc, int *iid);

void *nv_restart_(char *var, int *id);

int nvchkpt_all_(int *mype);

void* my_alloc_(unsigned int* n, char *s, int *iid);

void* nvread(char *var, int id);

#ifdef __cplusplus
}
#endif
