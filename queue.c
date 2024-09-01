#include "queue.h"

void init_queue(queue *q,int n)
{
    q->size = n;
    q->id = (pthread_t*)malloc(n * sizeof(pthread_t));
    q->front = 0;
    q->rear = 0;
}

void queue_push(queue *q,pthread_t tid)
{
    q->id[q->rear] = tid;
    q->rear = (q->rear + 1) % q->size;
    return;
}


pthread_t queue_pop(queue *q)
{
    pthread_t tid = q->id[q->front];
    q->front = (q->front + 1) % q->size;
    return tid;
}


int empty(queue *q)
{
    if(q->front == q->rear)
        return 1;
    return 0;
}

int full(queue *q)
{
    if((q->rear + 1) % q->size == q->front)
        return 1;
    return 0;
}

void deinit_queue(queue *q)
{
    free(q->id);
    q->front = 0;
    q->rear = 0;
    q->size = 0;
}