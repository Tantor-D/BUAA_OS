#include "lib.h"

void ptjoin(void *arg)
{
    writef("	going to exit\n");
	pthread_exit("xxxxxxpthread exit\n");
}

void umain() {
    int r;
    void * retval;
    pthread_t t;
    if ((r = pthread_create(&t, NULL, ptjoin, NULL)) < 0) 
		user_panic("	thread create failed\n");
	writef("	in testJoin, create t successfully, t =  0x%x\n", t);

	if ((r = pthread_join(t, &retval)) < 0) {	// 应该join成功
        writef("	waiting failed 1\n");
    }
	writef("	1  join successfully\n");
    writef("	retval = %s\n", (char*)retval);

    if ((r = pthread_join(t, &retval)) < 0)	// 应该join失败，t线程已结束
        writef("	waiting failed 2\n");

    return 0;
}
