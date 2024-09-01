#include "sbuf.h"

extern pthread_cond_t cond; // 外部全局变量

/* 初始化sbuf */
void sbuf_init(sbuf_t *b,int n)
{
    b->n  = n;  // 缓冲区大小为n
    b->count = 0;   // 初始化为0，因为一开始没有项目
    b->buf = calloc(n,sizeof(int)); // 动态分配内存，并置0
    b->rear = b->front = 0;     // 初始化头和尾的位置
    sem_init(&b->slot,0,n);     // 初始化空槽信号量
    sem_init(&b->item,0,0);     // 初始化项目信号量
    pthread_mutex_init(&b->mutex,NULL); // 初始化互斥信号量
}

/* 消耗sbuf */ 
void sbuf_deinit(sbuf_t *b)
{
    free(b->buf);
}

/* 插入项目 */
void sbuf_insert(sbuf_t *b,int item)
{
    P(&b->slot);    // 等待一个有一个空槽位
    // 给缓冲加锁，获得访问权
    pthread_mutex_lock(&b->mutex);
    b->buf[(++b->rear)%(b->n)] = item; // 添加项目
    ++b->count; // 新增了一个项目
    if(b->count == b->n)    // 缓冲区满
    {
        pthread_cond_signal(&cond); // 缓冲区满了，向管理者线程发生信号，通知条件发生改变
    }
    // 给缓冲解锁，释放访问权
    pthread_mutex_unlock(&b->mutex);
    V(&b->item);    // 宣布有新项目可用
}

/* 移除项目 */
int sbuf_remove(sbuf_t *b)
{
    P(&b->item);    // 等待有可用的项目
    // P(&b->mutex);   // 加锁获得缓冲区访问权
    pthread_mutex_lock(&b->mutex);
    int item = b->buf[(++b->front)%(b->n)]; // 取走项目
    --b->count;
    if(b->count == 0)    // 移除一个项目后缓冲区空
    {
        pthread_cond_signal(&cond); // 缓冲区空了，向管理者线程发生信号，通知条件发生改变
    }
    // V(&b->mutex);   // 解锁释放缓冲区访问权
    pthread_mutex_unlock(&b->mutex);
    V(&b->slot);    // 宣布有新的空槽使用
    return item;    // 返回项目
}

void P(sem_t *sem) 
{
    if (sem_wait(sem) < 0)
	    perror("P error");
}

void V(sem_t *sem) 
{
    if (sem_post(sem) < 0)
	    perror("V error");
}