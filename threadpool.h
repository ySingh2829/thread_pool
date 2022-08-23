#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct tpool_;
typedef struct tpool_ tpool_t;

typedef void (*thread_func_t)(void *arg);

tpool_t *threadpool_create(size_t num_of_threads);

ssize_t threadpool_add_work(tpool_t *pool,
        thread_func_t fn,
        void *arg);

void threadpool_wait(tpool_t *pool);

void threadpool_destroy(tpool_t *pool);

#endif
