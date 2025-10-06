#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct byte_buffer {
    char *data;
    size_t capacity;
    size_t rpos;
    size_t wpos;
} byte_buffer_t;

int buffer_init(byte_buffer_t *buf, size_t cap);
void buffer_reset(byte_buffer_t *buf);
void buffer_free(byte_buffer_t *buf);
size_t buffer_readable(const byte_buffer_t *buf);
size_t buffer_writable(const byte_buffer_t *buf);
ssize_t buffer_fill_from_fd(byte_buffer_t *buf, int fd);
ssize_t buffer_flush_to_fd(byte_buffer_t *buf, int fd);
int buffer_append(byte_buffer_t *buf, const char *data, size_t len);
const char *buffer_peek(const byte_buffer_t *buf);
void buffer_consume(byte_buffer_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif // BUFFER_H
