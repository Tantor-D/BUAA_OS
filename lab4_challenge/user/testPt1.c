#include "lib.h"
int i, j;
int size;

void basic(void *a) {
	while(1) {
		if(i <= j) {
			writef("    a[i] is %d\n", ((int *)a)[i]);
			i++;
		}
		if(i == 3)
			break;
	}
	writef("    basic end\n");
}

void umain() {
	int a[3] = {1, 22, 333};
	i = 0, j = 0, size = 3;
	pthread_t t;
	int ret = pthread_create(&t, NULL, basic,(void *)a);
	if(!ret){
		writef("    thread create success!\n");
	} else {
		writef("    thread create failed!\n");
		return 0;
	}
	while(1) {
		if(j <= i) {
			j++;
			writef("    j is %d\n", j);
		} 
		if(j == 3)
			break;
	}
	writef("    umain end\n");
}
