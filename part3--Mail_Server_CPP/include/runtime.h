#ifndef RUNTIME_H
#define RUNTIME_H

#include "config.h"
#include "thread_pool.h"
#include "concurrent_queue.h"
#include "max_heap.h"
#include "http.h"
#include "db.h"

#include <cstddef>

struct auth_context;
struct mail_service;
struct template_engine;

namespace mail {

class ConnectionTable;

struct ResponseJob {
    int fd{-1};
    http_response_t response{};
};

struct ServerRuntime {
    ServerConfig config{};
    int listen_fd{-1};
    int epoll_fd{-1};
    int event_fd{-1};
    thread_pool_t *pool{nullptr};
    concurrent_queue_t response_queue{};
    max_heap_t connection_heap{};
    ConnectionTable *connections{nullptr};
    db_handle_t *db{nullptr};
    auth_context *auth{nullptr};
    mail_service *mail{nullptr};
    template_engine *templates{nullptr};
};

} // namespace mail

using response_job_t = mail::ResponseJob;
using server_runtime_t = mail::ServerRuntime;

#endif // RUNTIME_H
