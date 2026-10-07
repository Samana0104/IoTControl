#pragma once

#include <pthread.h>
#include <stddef.h>

#define RTHREAD_POOL_MAX_WORKERS 16
#define RTHREAD_POOL_QUEUE_SIZE 128

typedef void (*RThreadPoolTask)(void *argument);

typedef struct _RThreadPoolJob
{
    RThreadPoolTask task;
    void *argument;
} RThreadPoolJob;

// 고정 개수 워커 + 고정 크기 작업 큐 (작업마다 메모리 할당 없음)
typedef struct _RThreadPool
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t workers[RTHREAD_POOL_MAX_WORKERS];
    int workerCount;
    RThreadPoolJob jobs[RTHREAD_POOL_QUEUE_SIZE];
    size_t head;
    size_t count;
    int stopping;
} RThreadPool;

// 워커 workerCount개(1..RTHREAD_POOL_MAX_WORKERS) 시작. 0: 성공, -1: 실패
int RThreadPoolStart(RThreadPool *pool, int workerCount);
// 작업을 큐에 넣음. 0: 넣음, -1: 큐 가득 참 또는 종료 중
int RThreadPoolSubmit(RThreadPool *pool, RThreadPoolTask task, void *argument);
// 큐에 남은 작업을 모두 처리한 뒤 워커 종료까지 대기
void RThreadPoolStop(RThreadPool *pool);
