#include "threadpool.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

struct wthread {
    thread_func_t fn;           // A function pointer to a start routine of a worker thread
    void *arg;                  // Argument for the start routine
    struct wthread *prev;       // Points to the previous worker thread 
};
typedef struct wthread wthread_t;


struct tpool_ {
    wthread_t *first;           // Pointer to the first worker in queue
    wthread_t *last;            // Pointer to the last worker in queue
    pthread_mutex_t mtx;        // Mutex condition for locking a thread
    pthread_cond_t work_cond;   // Signal threads to start processing
    pthread_cond_t idle_cond;   // Signals when there are no threads processing
    size_t working_cnt;         // No. of threads processing
    size_t thread_cnt;          // No of threads
    bool stopped;
};

// Push work to queue
static void
tpool_work_push(tpool_t *pool, wthread_t *worker)
{
    // queue is empty
    if (pool->first == NULL) {
        pool->first = worker;
        pool->last = pool->first;
    } else {
        pool->last->prev = worker;
        pool->last = worker;
    }
}

// Pull work from the queue
static wthread_t *
tpool_work_pull(tpool_t *pool)
{
    wthread_t *worker;

    // If thread_pool is no more.
    if (pool == NULL)
        return NULL;

    worker = pool->first;
    if (worker == NULL)     // If queue is empty
        return NULL;

    if (worker->prev == NULL) {     // Last worker thread in queue, update first and last pointer
        pool->first = NULL;
        pool->last = NULL;
    } else
        pool->first = worker->prev;

    return worker;
}

static void
tpool_work_free(wthread_t *worker)
{
    if (worker == NULL)
        return;
    free(worker);
}

static wthread_t *
tpool_worker_create(thread_func_t fn, void *arg)
{
    wthread_t *worker;

    if (fn == NULL)
        return NULL;

    worker = malloc(sizeof(wthread_t));
    worker->fn = fn;
    worker->arg = arg;
    worker->prev = NULL;

    return worker;
}

static void *
tpool_worker_routine(void *arg)
{
    tpool_t *pool = arg;
    wthread_t *worker;

    while (1) {
        pthread_mutex_lock(&(pool->mtx));

        // Waits for work to be avialable
        while (pool->first == NULL && !pool->stopped)
            pthread_cond_wait(&(pool->work_cond), &(pool->mtx));

        if (pool->stopped)
            break;

        worker = tpool_work_pull(pool);
        pool->working_cnt++;
        pthread_mutex_unlock(&(pool->mtx));

        // Start processing the worker threads
        // No mutex here for concurrency
        if (worker != NULL) {
            thread_func_t fn = worker->fn;
            void *arg = worker->arg;
            fn(arg);                    // Worker thread routine
            tpool_work_free(worker);
        }

        pthread_mutex_lock(&(pool->mtx));
        pool->working_cnt--;
        if (!pool->stopped && pool->working_cnt == 0 && pool->first == NULL)
            pthread_cond_signal(&(pool->idle_cond));
        pthread_mutex_unlock(&(pool->mtx));
    }
    pool->thread_cnt--;
    pthread_cond_signal(&(pool->idle_cond));
    pthread_mutex_unlock(&(pool->mtx));

    return NULL;
}

tpool_t *
threadpool_create(size_t num_of_threads)
{
    tpool_t *pool;
    pthread_t thread;

    if (num_of_threads == 0)
        num_of_threads = 2;


    pool = calloc(1, sizeof(tpool_t));
    pool->thread_cnt = num_of_threads;

    if (pool == NULL) {
        fprintf(stderr, "Problem creating thread pool");
        exit(1);
    }


    pthread_mutex_init(&(pool->mtx), NULL);
    pthread_cond_init(&(pool->work_cond), NULL);
    pthread_cond_init(&(pool->idle_cond), NULL);

    for (int i = 0; i < num_of_threads; i++) {
        pthread_create(&thread, NULL, tpool_worker_routine, pool);
        pthread_detach(thread);
    }

    return pool;
}

ssize_t
threadpool_add_work(tpool_t *pool, thread_func_t fn, void *arg)
{
    wthread_t *worker;

    if (pool == NULL)
        return -1;

    worker = tpool_worker_create(fn, arg);
    if (worker == NULL)
        return -1;

    pthread_mutex_lock(&(pool->mtx));
    tpool_work_push(pool, worker);

    // Since we have pushed work in the queue
    // we broadcast to every waiting thread that work is available
    pthread_cond_broadcast(&(pool->work_cond));
    pthread_mutex_unlock(&(pool->mtx));

    return 0;
}

void
threadpool_wait(tpool_t *pool)
{
    if (pool == NULL)
        return;

    pthread_mutex_lock(&(pool->mtx));
    while (1) {
        if ((!pool->stopped && pool->working_cnt != 0) || (pool->stopped && pool->thread_cnt != 0))
            pthread_cond_wait(&(pool->idle_cond), &(pool->mtx));
        else
            break;
    }
    pthread_mutex_unlock(&(pool->mtx));
}

void
threadpool_destroy(tpool_t *pool)
{
    wthread_t *worker1, *worker2;

    if (pool == NULL)
        return;

    pthread_mutex_lock(&(pool->mtx));
    worker1 = pool->first;

    // Clear all pending work
    while (worker1 != NULL) {
        worker2 = worker1->prev;
        tpool_work_free(worker1);
        worker1 = worker2;
    }
    pool->stopped = true;
    pthread_cond_broadcast(&(pool->work_cond));
    pthread_mutex_unlock(&(pool->mtx));

    // Wait for running processes on worker threads to finish
    threadpool_wait(pool);

    pthread_mutex_destroy(&(pool->mtx));
    pthread_cond_destroy(&(pool->work_cond));
    pthread_cond_destroy(&(pool->idle_cond));

    free(pool);
}
