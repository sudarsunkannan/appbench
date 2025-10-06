#define __USE_GNU

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>

/* The semaphore key is an arbitrary long integer which serves as an
   external identifier by which the semaphore is known to any program
   that wishes to use it. */

#define KEY (1492)

int gt_create_sema()
{
   int id; /* Number by which the semaphore is known within a program */

   /* The next thing is an argument to the semctl() function. Semctl() 
      does various things to the semaphore depending on which arguments
      are passed. We will use it to make sure that the value of the 
      semaphore is initially 0. */

   union semun {
	int val;
	struct semid_ds *buf;
	ushort * array;
   } argument;

   argument.val = 0;

   /* Create the semaphore with external key KEY if it doesn't already 
      exists. Give permissions to the world. */

   id = semget(KEY, 1, 0666 | IPC_CREAT);

   /* Always check system returns. */

   if(id < 0)
   {
      fprintf(stderr, "Unable to obtain semaphore.\n");
      //exit(0);
   }

   /* What we actually get is an array of semaphores. The second 
      argument to semget() was the array dimension - in our case
      1. */

   /* Set the value of the number 0 semaphore in semaphore array
      # id to the value 0. */

   if( semctl(id, 0, SETVAL, argument) < 0)
   {
#ifdef _DEBUG
      fprintf( stderr, "Cannot set semaphore value.\n");
#endif
      return -1; 
   }
   else
   {
#ifdef _DEBUG
      fprintf(stderr, "Semaphore %d initialized.\n", KEY);
#endif
   }

    return 0;
}


int gt_incr_sema()
{
   int id;  /* Internal identifier of the semaphore. */
   struct sembuf operations[1];
   /* An "array" of one operation to perform on the semaphore. */

   int retval; /* Return value from semop() */

   /* Get the index for the semaphore with external name KEY. */
   id = semget(KEY, 1, 0666);
   if(id < 0)
   /* Semaphore does not exist. */
   {
      fprintf(stderr, "Program sema cannot find semaphore, exiting.\n");
      //exit(0);
   }

#ifdef _DEBUG
   /* Do a semaphore V-operation. */
   printf("Program sema about to do a V-operation. \n");
#endif

   /* Set up the sembuf structure. */
   /* Which semaphore in the semaphore array : */
    operations[0].sem_num = 0;
    /* Which operation? Add 1 to semaphore value : */
    operations[0].sem_op = 1;
    /* Set the flag so we will wait : */   
    operations[0].sem_flg = 0;

    /* So do the operation! */
    retval = semop(id, operations, 1);

    if(retval == 0)
    {
#ifdef _DEBUG
       printf("Successful V-operation by program sema.\n");
#endif
    }
    else
    {
#ifdef _DEBUG
       printf("sema: V-operation did not succeed.\n");
	perror("REASON");
#endif
        return -1;
    }
    return 0; 
}


int gt_decr_sema()
{
   int id;  /* Internal identifier of the semaphore. */
   struct sembuf operations[1];
   /* An "array" of one operation to perform on the semaphore. */

   int retval; /* Return value from semop() */

   /* Get the index for the semaphore with external name KEY. */
   id = semget(KEY, 1, 0666);
   if(id < 0)
   /* Semaphore does not exist. */
   {
      fprintf(stderr, "Program semb cannot find semaphore, exiting.\n");
      //exit(0);
   }

#ifdef _DEBUG
   /* Do a semaphore P-operation. */
   printf("Program semb about to do a P-operation. \n");
   printf("Process id is %d\n", getpid());
#endif

   /* Set up the sembuf structure. */
   /* Which semaphore in the semaphore array : */
    operations[0].sem_num = 0;
    /* Which operation? Subtract 1 from semaphore value : */
    operations[0].sem_op = -1;
    /* Set the flag so we will wait : */   
    operations[0].sem_flg = 0;

    /* So do the operation! */
    retval = semop(id, operations, 1);

    if(retval == 0)
    {
#ifdef _DEBUG
       printf("Successful P-operation by program semb.\n");
       printf("Process id is %d\n", getpid());
#endif
    }
    else
    {
#ifdef _DEBUG
       printf("semb: P-operation did not succeed.\n");
#endif
       return -1;
    }
    return 0; 
}









