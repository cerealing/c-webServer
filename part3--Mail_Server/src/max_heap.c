#include "max_heap.h"

#include <stdlib.h>
#include <string.h>

static void heap_swap(heap_node_t *a, heap_node_t *b) {
    heap_node_t tmp = *a;
    *a = *b;
    *b = tmp;
}

static void heapify_up(max_heap_t *heap, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (heap->nodes[idx].priority <= heap->nodes[parent].priority) {
            break;
        }
        heap_swap(&heap->nodes[idx], &heap->nodes[parent]);
        idx = parent;
    }
}

static void heapify_down(max_heap_t *heap, size_t idx) {
    while (1) {
        size_t left = idx * 2 + 1;
        size_t right = idx * 2 + 2;
        size_t largest = idx;
        if (left < heap->size && heap->nodes[left].priority > heap->nodes[largest].priority) {
            largest = left;
        }
        if (right < heap->size && heap->nodes[right].priority > heap->nodes[largest].priority) {
            largest = right;
        }
        if (largest == idx) break;
        heap_swap(&heap->nodes[idx], &heap->nodes[largest]);
        idx = largest;
    }
}

int heap_init(max_heap_t *heap, size_t capacity) {
    heap->nodes = calloc(capacity, sizeof(heap_node_t));
    if (!heap->nodes) return -1;
    heap->size = 0;
    heap->capacity = capacity;
    return 0;
}

void heap_free(max_heap_t *heap) {
    free(heap->nodes);
    heap->nodes = NULL;
    heap->size = heap->capacity = 0;
}

int heap_push(max_heap_t *heap, heap_node_t node) {
    if (heap->size == heap->capacity) {
        size_t new_cap = heap->capacity * 2;
        heap_node_t *new_nodes = realloc(heap->nodes, new_cap * sizeof(heap_node_t));
        if (!new_nodes) return -1;
        heap->nodes = new_nodes;
        heap->capacity = new_cap;
    }
    heap->nodes[heap->size] = node;
    heapify_up(heap, heap->size);
    heap->size++;
    return 0;
}

int heap_pop(max_heap_t *heap, heap_node_t *out) {
    if (heap->size == 0) return -1;
    if (out) *out = heap->nodes[0];
    heap->nodes[0] = heap->nodes[heap->size - 1];
    heap->size--;
    heapify_down(heap, 0);
    return 0;
}

int heap_peek(max_heap_t *heap, heap_node_t *out) {
    if (heap->size == 0) return -1;
    if (out) *out = heap->nodes[0];
    return 0;
}

int heap_remove_fd(max_heap_t *heap, int fd) {
    for (size_t i = 0; i < heap->size; ++i) {
        if (heap->nodes[i].key_fd == fd) {
            heap->nodes[i] = heap->nodes[heap->size - 1];
            heap->size--;
            heapify_down(heap, i);
            heapify_up(heap, i);
            return 0;
        }
    }
    return -1;
}
