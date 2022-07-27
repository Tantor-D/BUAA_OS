#include "lib.h"
pthread_t t1;

void testexit(void *a)
{
	int retval1 = -9999;
	writef("	thread %x exit\n", t1);
	pthread_exit(&retval1);
}

void umain()
{
	int a[3] = {1, 2, 3};
	int ret1 = pthread_create(&t1, NULL, testexit, (void *)a);
	if (!ret1)
		writef("	thread create successful!\n");
	while (env->env_threads[TCBX(t1)].tcb_status != ENV_FREE)
	{
		writef("	sssss here\n");
	}
	writef("	retval: = %d\n", *((int *)env->env_threads[TCBX(t1)].tcb_exit_ptr));
	writef("	exit test end!\n");
}

