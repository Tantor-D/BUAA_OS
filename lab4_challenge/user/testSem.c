#include "lib.h"

pthread_t t1, t2, t3;
sem_t yy, xx; 

void test1() {
	writef("	here is test1\n");
	sem_post(&xx);
}

void test2() {
	sem_wait(&xx);
	writef("	here is test2\n");
	sem_post(&yy);
}

void test3() {
	sem_wait(&yy);
	writef("	here is test3\n");
}

void umain() {
	sem_init(&yy, 0, 0);
	sem_init(&xx, 0, 0);
	pthread_create(&t3, NULL, test3, NULL);
	pthread_create(&t2, NULL, test2, NULL);
	pthread_create(&t1, NULL, test1, NULL);
}

