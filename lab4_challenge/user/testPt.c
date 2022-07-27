#include "lib.h"
int count1;
int count2;
void *test(void *arg) {
	int arg1 = ((int *)arg)[0];
	int arg2 = ((int *)arg)[1];
	int arg3 = ((int *)arg)[2];
	writef("	arg 1 is %d\n",arg1);
	writef("	arg 2 is %d\n",arg2);
	writef("	arg 3 is %d\n",arg3);
	writef("	count1 is %d\n",count1);
	++count2;
	writef("	b is change\n");
	while (1) {
		writef("yyy\n");
		if (count1 != 1)
			break;
	}
	writef("	count1 is %d\n",count1);
}
void umain() {
	count1 = 0;
	count2 = 0;
	++count1;
	int thread;
	int args[3];
	args[0] = 1;
	args[1] = 2;
	args[2] = 3;
	pthread_t son;
	thread = pthread_create(&son,NULL,test,(void *) args);
	writef("	create successful\n");
	if (!thread) {
		while (1) {
			writef("xxx\n");
			if (count2 != 0)
				break;
		}
		++count1;
		writef("	thread1 is out\n");	
	}	
}


