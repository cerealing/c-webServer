#ifndef MAX_HEAP_H
#define MAX_HEAP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct heap_node {
    int key_fd;
    long long priority;
} heap_node_t;

typedef struct max_heap {
    heap_node_t *nodes;
    size_t size;
    size_t capacity;
} max_heap_t;

int heap_init(max_heap_t *heap, size_t capacity);
void heap_free(max_heap_t *heap);
int heap_push(max_heap_t *heap, heap_node_t node);
int heap_pop(max_heap_t *heap, heap_node_t *out);
int heap_peek(max_heap_t *heap, heap_node_t *out);
int heap_remove_fd(max_heap_t *heap, int fd);

#ifdef __cplusplus
}
#endif

#endif // MAX_HEAP_H
