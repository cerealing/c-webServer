#include "buffer.h"

#include <cstdlib>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int buffer_init(byte_buffer_t *buf, size_t cap) {
    buf->data = static_cast<char*>(std::malloc(cap));
    if (!buf->data) return -1;
    buf->capacity = cap;
    buf->rpos = 0;
    buf->wpos = 0;
    return 0;
}

void buffer_reset(byte_buffer_t *buf) {
    buf->rpos = 0;
    buf->wpos = 0;
}

void buffer_free(byte_buffer_t *buf) {
    std::free(buf->data);
    buf->data = NULL;
    buf->capacity = buf->rpos = buf->wpos = 0;
}

size_t buffer_readable(const byte_buffer_t *buf) {
    return buf->wpos - buf->rpos;
}

size_t buffer_writable(const byte_buffer_t *buf) {
    return buf->capacity - buf->wpos;
}

ssize_t buffer_fill_from_fd(byte_buffer_t *buf, int fd) {
    if (buffer_writable(buf) == 0) {
        size_t new_cap = buf->capacity * 2;
    char *new_data = static_cast<char*>(std::realloc(buf->data, new_cap));
        if (!new_data) {
            errno = ENOMEM;
            return -1;
        }
        buf->data = new_data;
        buf->capacity = new_cap;
    }
    ssize_t n = read(fd, buf->data + buf->wpos, buf->capacity - buf->wpos);
    if (n > 0) {
        buf->wpos += (size_t)n;
    }
    return n;
}

ssize_t buffer_flush_to_fd(byte_buffer_t *buf, int fd) {
    size_t readable = buffer_readable(buf);
    if (readable == 0) return 0;
    ssize_t n = write(fd, buf->data + buf->rpos, readable);
    if (n > 0) {
        buf->rpos += (size_t)n;
        if (buf->rpos == buf->wpos) {
            buf->rpos = buf->wpos = 0;
        }
    }
    return n;
}

int buffer_append(byte_buffer_t *buf, const char *data, size_t len) {
    while (buffer_writable(buf) < len) {
        size_t new_cap = buf->capacity * 2;
    char *new_data = static_cast<char*>(std::realloc(buf->data, new_cap));
        if (!new_data) {
            return -1;
        }
        buf->data = new_data;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->wpos, data, len);
    buf->wpos += len;
    return 0;
}

const char *buffer_peek(const byte_buffer_t *buf) {
    return buf->data + buf->rpos;
}

void buffer_consume(byte_buffer_t *buf, size_t len) {
    if (len >= buffer_readable(buf)) {
        buf->rpos = buf->wpos = 0;
    } else {
        buf->rpos += len;
    }
}
