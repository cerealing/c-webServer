#include "concurrent_queue.h"

#include <stdlib.h>

int cq_init(concurrent_queue_t *q) {
    q->head = q->tail = NULL;
    q->size = 0;
    if (pthread_mutex_init(&q->mutex, NULL) != 0) return -1;
    return 0;
}

void cq_destroy(concurrent_queue_t *q, void (*free_fn)(void *)) {
    pthread_mutex_lock(&q->mutex);
    cq_node_t *node = q->head;
    while (node) {
        cq_node_t *next = node->next;
        if (free_fn) free_fn(node->data);
        free(node);
        node = next;
    }
    q->head = q->tail = NULL;
    q->size = 0;
    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
}

void cq_push(concurrent_queue_t *q, void *data) {
    cq_node_t *node = malloc(sizeof(*node));
    node->data = data;
    node->next = NULL;
    pthread_mutex_lock(&q->mutex);
    if (q->tail) q->tail->next = node;
    q->tail = node;
    if (!q->head) q->head = node;
    q->size++;
    pthread_mutex_unlock(&q->mutex);
}

void *cq_pop(concurrent_queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    cq_node_t *node = q->head;
    if (!node) {
        pthread_mutex_unlock(&q->mutex);
        return NULL;
    }
    q->head = node->next;
    if (!q->head) q->tail = NULL;
    q->size--;
    pthread_mutex_unlock(&q->mutex);
    void *data = node->data;
    free(node);
    return data;
}

size_t cq_size(concurrent_queue_t *q) {
    pthread_mutex_lock(&q->mutex);
    size_t s = q->size;
    pthread_mutex_unlock(&q->mutex);
    return s;
}
