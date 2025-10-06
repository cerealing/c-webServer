#ifndef CONCURRENT_QUEUE_H
#define CONCURRENT_QUEUE_H

#include <stddef.h>
#include <pthread.h>

typedef struct cq_node {
    void *data;
    struct cq_node *next;
} cq_node_t;

typedef struct concurrent_queue {
    cq_node_t *head;
    cq_node_t *tail;
    size_t size;
    pthread_mutex_t mutex;
} concurrent_queue_t;

int cq_init(concurrent_queue_t *q);
void cq_destroy(concurrent_queue_t *q, void (*free_fn)(void *));
void cq_push(concurrent_queue_t *q, void *data);
void *cq_pop(concurrent_queue_t *q);
size_t cq_size(concurrent_queue_t *q);

#endif // CONCURRENT_QUEUE_H
