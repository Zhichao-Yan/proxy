#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stdlib.h>

typedef struct queue{
    int front;
    int rear;
    pthread_t *id;
    int size;
}queue;

void init_queue(queue *q,int n);
void deinit_queue(queue *q);
void queue_push(queue *q,pthread_t tid);
pthread_t queue_pop(queue *q);
int empty(queue *q);
int full(queue *q);



#endif