#ifndef CONNECTION_H
#define CONNECTION_H

#include <stdint.h>
#include <sys/epoll.h>
#include "buffer.h"
#include "http_parser.h"
#include "http.h"

#define READ_BUFFER_SIZE 16384
#define WRITE_BUFFER_SIZE 32768

typedef enum {
    CONN_STATE_READING,
    CONN_STATE_PROCESSING,
    CONN_STATE_WRITING,
    CONN_STATE_CLOSING
} conn_state_t;

typedef struct connection {
    int fd;
    conn_state_t state;
    byte_buffer_t read_buf;
    byte_buffer_t write_buf;
    http_parser_t parser;
    http_response_t response;
    long long last_activity_ms;
    int registered_events;
    int keep_alive;
} connection_t;

int connection_init(connection_t *c, int fd);
void connection_free(connection_t *c);
int connection_handle_read(connection_t *c);
int connection_handle_write(connection_t *c);
void connection_prepare_response(connection_t *c, const http_response_t *res);

#endif // CONNECTION_H
