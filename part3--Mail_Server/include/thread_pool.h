#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tp_job {
    void (*fn)(void *arg);
    void *arg;
} tp_job_t;

typedef struct thread_pool thread_pool_t;

typedef void (*tp_error_cb)(const char *msg);

typedef struct thread_pool_config {
    size_t thread_count;
    size_t queue_capacity;
    tp_error_cb on_error;
} thread_pool_config_t;

thread_pool_t *thread_pool_create(const thread_pool_config_t *cfg);
void thread_pool_destroy(thread_pool_t *pool);
int thread_pool_submit(thread_pool_t *pool, tp_job_t job);
size_t thread_pool_size(const thread_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif // THREAD_POOL_H
