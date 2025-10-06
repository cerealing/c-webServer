#ifndef RUNTIME_H
#define RUNTIME_H

#include "config.h"
#include "thread_pool.h"
#include "concurrent_queue.h"
#include "max_heap.h"
#include "http.h"
#include "db.h"

#include <sys/epoll.h>

typedef struct response_job {
    int fd;
    http_response_t response;
} response_job_t;

typedef struct connection_table connection_table_t;

typedef struct auth_context auth_context_t;
typedef struct mail_service mail_service_t;
typedef struct template_engine template_engine_t;

typedef struct server_runtime {
    server_config config;
    int listen_fd;
    int epoll_fd;
    int event_fd;
    thread_pool_t *pool;
    concurrent_queue_t response_queue;
    max_heap_t connection_heap;
    connection_table_t *connections;
    db_handle_t *db;
    auth_context_t *auth;
    mail_service_t *mail;
    template_engine_t *templates;
} server_runtime_t;

#endif // RUNTIME_H
