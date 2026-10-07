#include "RThreadPool.h"

#include <errno.h>
#include <string.h>

static void *RunWorker(void *arg);

int RThreadPoolStart(RThreadPool *pool, int workerCount)
{
    if(pool == NULL || workerCount < 1 || workerCount > RTHREAD_POOL_MAX_WORKERS)
    {
        errno = EINVAL;
        return -1;
    }
    memset(pool, 0, sizeof(*pool));
    if(pthread_mutex_init(&pool->mutex, NULL) != 0 || pthread_cond_init(&pool->cond, NULL) != 0)
    {
        return -1;
    }
    for(int index = 0; index < workerCount; ++index)
    {
        int createResult = pthread_create(&pool->workers[index], NULL, RunWorker, pool);

        if(createResult != 0)
        {
            RThreadPoolStop(pool);
            errno = createResult;
            return -1;
        }
        pool->workerCount++;
    }
    return 0;
}

int RThreadPoolSubmit(RThreadPool *pool, RThreadPoolTask task, void *argument)
{
    int result = -1;

    if(pool == NULL || task == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&pool->mutex);
    if(!pool->stopping && pool->count < RTHREAD_POOL_QUEUE_SIZE)
    {
        RThreadPoolJob *job = &pool->jobs[(pool->head + pool->count) % RTHREAD_POOL_QUEUE_SIZE];

        job->task = task;
        job->argument = argument;
        pool->count++;
        pthread_cond_signal(&pool->cond);
        result = 0;
    }
    pthread_mutex_unlock(&pool->mutex);
    if(result != 0)
    {
        errno = EAGAIN;
    }
    return result;
}

void RThreadPoolStop(RThreadPool *pool)
{
    pthread_mutex_lock(&pool->mutex);
    pool->stopping = 1;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    for(int index = 0; index < pool->workerCount; ++index)
    {
        pthread_join(pool->workers[index], NULL);
    }
    pool->workerCount = 0;
}

static void *RunWorker(void *arg)
{
    RThreadPool *pool = (RThreadPool *)arg;

    while(1)
    {
        RThreadPoolJob job;

        pthread_mutex_lock(&pool->mutex);
        while(pool->count == 0 && !pool->stopping)
        {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }
        // 종료 요청 후에도 남은 작업은 처리
        if(pool->count == 0)
        {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
        job = pool->jobs[pool->head];
        pool->head = (pool->head + 1) % RTHREAD_POOL_QUEUE_SIZE;
        pool->count--;
        pthread_mutex_unlock(&pool->mutex);

        job.task(job.argument);
    }
    return NULL;
}
