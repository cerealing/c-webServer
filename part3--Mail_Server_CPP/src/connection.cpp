#include "connection.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

int connection_init(connection_t *c, int fd) {
    memset(c, 0, sizeof(*c));
    c->fd = fd;
    c->state = CONN_STATE_READING;
    buffer_init(&c->read_buf, READ_BUFFER_SIZE);
    buffer_init(&c->write_buf, WRITE_BUFFER_SIZE);
    http_parser_init(&c->parser);
    http_response_init(&c->response);
    c->keep_alive = 1;
    c->last_activity_ms = util_now_ms();
    return 0;
}

void connection_free(connection_t *c) {
    buffer_free(&c->read_buf);
    buffer_free(&c->write_buf);
    http_request_free(&c->parser.request);
    http_response_free(&c->response);
    close(c->fd);
    c->fd = -1;
}

int connection_handle_read(connection_t *c) {
    ssize_t n = buffer_fill_from_fd(&c->read_buf, c->fd);
    if (n == 0) {
        return -1; // peer closed
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    c->last_activity_ms = util_now_ms();
    return 0;
}

int connection_handle_write(connection_t *c) {
    ssize_t n = buffer_flush_to_fd(&c->write_buf, c->fd);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    if (buffer_readable(&c->write_buf) == 0) {
        if (c->keep_alive) {
            http_parser_reset(&c->parser);
            http_response_reset(&c->response);
            c->state = CONN_STATE_READING;
        } else {
            c->state = CONN_STATE_CLOSING;
        }
    }
    return 0;
}

void connection_prepare_response(connection_t *c, const http_response_t *res) {
    buffer_reset(&c->write_buf);

    char header[1024];
    int len = snprintf(header, sizeof(header),
        "%s %d %s\r\n",
        "HTTP/1.1", res->status_code, res->status_text);
    buffer_append(&c->write_buf, header, len);

    for (size_t i = 0; i < res->header_count; ++i) {
        len = snprintf(header, sizeof(header), "%s: %s\r\n",
                       res->headers[i].name, res->headers[i].value);
        buffer_append(&c->write_buf, header, len);
    }

    len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n", res->body_length);
    buffer_append(&c->write_buf, header, len);

    len = snprintf(header, sizeof(header), "Connection: %s\r\n\r\n",
                   res->keep_alive ? "keep-alive" : "close");
    buffer_append(&c->write_buf, header, len);

    if (res->body && res->body_length > 0) {
        buffer_append(&c->write_buf, res->body, res->body_length);
    }

    c->state = CONN_STATE_WRITING;
    c->keep_alive = res->keep_alive;
    c->last_activity_ms = util_now_ms();
}
