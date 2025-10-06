#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>


int thread1(void* userdata)
{
	__time_t t0;
	int delta_t,_delta_t;
	struct timeval tv;
	pthread_mutex_t* mut = (pthread_mutex_t*) userdata;

	gettimeofday(&tv,0);
	t0 = tv.tv_sec;

	while (1)
	{
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
		pthread_mutex_lock(mut);
		usleep(10000);
		gettimeofday(&tv,0);
		delta_t	= (int) (tv.tv_sec - t0);
		if (delta_t != _delta_t)
			fprintf(stderr,"%d",delta_t);
		_delta_t = delta_t;
		pthread_mutex_unlock(mut);
		//usleep(1000); //uncomment to see difference.
	}
	return 0;
}

main() {


}
