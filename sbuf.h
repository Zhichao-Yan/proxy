#ifndef __SBUF_H__
#define __SBUF_H__

#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    int n;          // 槽位的最大数量
    int count;      // 实际项目的数量
    int *buf;       // 动态分配的数组
    int front;      // 指向项目数组的第一个项目
    int rear;       // 指向项目数组的最后一个项目
    sem_t slot;     // 空槽位的数量
    sem_t item;     // 项目的数量
    pthread_mutex_t mutex; // 提供互斥的缓冲区访问
}sbuf_t;

void sbuf_init(sbuf_t *b,int n);
void sbuf_deinit(sbuf_t *b);
void sbuf_insert(sbuf_t *b,int item);
int sbuf_remove(sbuf_t *b);
void P(sem_t *sem);
void V(sem_t *sem);
#endif 