#include "thread_pool.h"

#include <cstdlib>
#include <cstring>
#include <errno.h>

typedef struct job_queue {
    tp_job_t *jobs;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t size;
} job_queue_t;

struct thread_pool {
    pthread_t *threads;
    size_t thread_count;
    job_queue_t queue;
    pthread_mutex_t mutex;
    pthread_cond_t cond_jobs;
    pthread_cond_t cond_empty;
    int shutting_down;
    tp_error_cb on_error;
};

static void job_queue_init(job_queue_t *q, size_t cap) {
    q->jobs = static_cast<tp_job_t*>(std::calloc(cap, sizeof(tp_job_t)));
    q->capacity = cap;
    q->head = 0;
    q->tail = 0;
    q->size = 0;
}

static void job_queue_destroy(job_queue_t *q) {
    std::free(q->jobs);
    memset(q, 0, sizeof(*q));
}

static int job_queue_push(job_queue_t *q, tp_job_t job) {
    if (q->size == q->capacity) {
        return -1;
    }
    q->jobs[q->tail] = job;
    q->tail = (q->tail + 1) % q->capacity;
    q->size++;
    return 0;
}

static int job_queue_pop(job_queue_t *q, tp_job_t *out) {
    if (q->size == 0) return -1;
    *out = q->jobs[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->size--;
    return 0;
}

static void *worker_main(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;
    while (1) {
        pthread_mutex_lock(&pool->mutex);
        while (!pool->shutting_down && pool->queue.size == 0) {
            pthread_cond_wait(&pool->cond_jobs, &pool->mutex);
        }

        if (pool->shutting_down && pool->queue.size == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        tp_job_t job = {0};
        if (job_queue_pop(&pool->queue, &job) != 0) {
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }

        if (pool->queue.size == 0) {
            pthread_cond_signal(&pool->cond_empty);
        }
        pthread_mutex_unlock(&pool->mutex);

        if (job.fn) {
            job.fn(job.arg);
        }
    }
    return NULL;
}

thread_pool_t *thread_pool_create(const thread_pool_config_t *cfg) {
    if (!cfg || cfg->thread_count == 0 || cfg->queue_capacity == 0) {
        errno = EINVAL;
        return NULL;
    }

    thread_pool_t *pool = static_cast<thread_pool_t*>(std::calloc(1, sizeof(*pool)));
    if (!pool) {
        return NULL;
    }

    pool->thread_count = cfg->thread_count;
    pool->threads = static_cast<pthread_t*>(std::calloc(pool->thread_count, sizeof(pthread_t)));
    pool->on_error = cfg->on_error;
    job_queue_init(&pool->queue, cfg->queue_capacity);

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond_jobs, NULL);
    pthread_cond_init(&pool->cond_empty, NULL);

    for (size_t i = 0; i < pool->thread_count; ++i) {
        int rc = pthread_create(&pool->threads[i], NULL, worker_main, pool);
        if (rc != 0) {
            if (pool->on_error) pool->on_error("pthread_create failed");
            pool->shutting_down = 1;
            pthread_cond_broadcast(&pool->cond_jobs);
            for (size_t j = 0; j < i; ++j) {
                pthread_join(pool->threads[j], NULL);
            }
            thread_pool_destroy(pool);
            errno = rc;
            return NULL;
        }
    }

    return pool;
}

void thread_pool_destroy(thread_pool_t *pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->mutex);
    pool->shutting_down = 1;
    pthread_cond_broadcast(&pool->cond_jobs);
    pthread_mutex_unlock(&pool->mutex);

    for (size_t i = 0; i < pool->thread_count; ++i) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond_jobs);
    pthread_cond_destroy(&pool->cond_empty);
    job_queue_destroy(&pool->queue);
    std::free(pool->threads);
    std::free(pool);
}

int thread_pool_submit(thread_pool_t *pool, tp_job_t job) {
    if (!pool || !job.fn) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&pool->mutex);
    while (!pool->shutting_down && pool->queue.size == pool->queue.capacity) {
        pthread_cond_wait(&pool->cond_empty, &pool->mutex);
    }
    if (pool->shutting_down) {
        pthread_mutex_unlock(&pool->mutex);
        errno = ECANCELED;
        return -1;
    }

    int rc = job_queue_push(&pool->queue, job);
    if (rc != 0) {
        if (pool->on_error) pool->on_error("job queue overflow");
        pthread_mutex_unlock(&pool->mutex);
        errno = EAGAIN;
        return -1;
    }
    pthread_cond_signal(&pool->cond_jobs);
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

size_t thread_pool_size(const thread_pool_t *pool) {
    if (!pool) return 0;
    return pool->thread_count;
}
