#include "runtime.h"
#include "config.h"
#include "util.h"
#include "logger.h"
#include "connection.h"
#include "router.h"
#include "jobs.h"

#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <netinet/tcp.h>

#define MAX_EVENTS 128

typedef struct connection_table {
    connection_t **slots;
    size_t capacity;
    size_t count;
} connection_table_t;

static int connection_table_init(connection_table_t *table, size_t capacity) {
    table->slots = static_cast<connection_t**>(std::calloc(capacity, sizeof(connection_t *)));
    if (!table->slots) return -1;
    table->capacity = capacity;
    table->count = 0;
    return 0;
}

static void connection_table_free(connection_table_t *table) {
    if (!table) return;
    for (size_t i = 0; i < table->capacity; ++i) {
        if (table->slots[i]) {
            connection_free(table->slots[i]);
            std::free(table->slots[i]);
        }
    }
    std::free(table->slots);
    table->slots = NULL;
    table->capacity = table->count = 0;
}

static connection_t *connection_table_get(connection_table_t *table, int fd) {
    if ((size_t)fd >= table->capacity) return NULL;
    return table->slots[fd];
}

static int connection_table_put(connection_table_t *table, connection_t *conn) {
    if ((size_t)conn->fd >= table->capacity) {
        size_t new_cap = table->capacity;
        while ((size_t)conn->fd >= new_cap) {
            new_cap *= 2;
        }
    connection_t **new_slots = static_cast<connection_t**>(std::realloc(table->slots, new_cap * sizeof(connection_t *)));
        if (!new_slots) return -1;
        memset(new_slots + table->capacity, 0, (new_cap - table->capacity) * sizeof(connection_t *));
        table->slots = new_slots;
        table->capacity = new_cap;
    }
    if (!table->slots[conn->fd]) table->count++;
    table->slots[conn->fd] = conn;
    return 0;
}

static void connection_table_remove(connection_table_t *table, int fd) {
    if ((size_t)fd >= table->capacity) return;
    if (table->slots[fd]) {
        connection_free(table->slots[fd]);
    std::free(table->slots[fd]);
        table->slots[fd] = NULL;
        if (table->count > 0) table->count--;
    }
}

static int setup_listen_socket(const server_config *cfg) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg->port);
    addr.sin_addr.s_addr = inet_addr(cfg->listen_address);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, cfg->max_connections) < 0) {
        close(fd);
        return -1;
    }
    util_set_nonblocking(fd);
    util_set_cloexec(fd);
    return fd;
}

static void notify_main(server_runtime_t *rt) {
    uint64_t one = 1;
    ssize_t written = write(rt->event_fd, &one, sizeof(one));
    (void)written;
}

static void handle_worker_response(server_runtime_t *rt, connection_table_t *table) {
    uint64_t val;
    while (read(rt->event_fd, &val, sizeof(val)) > 0) {}

    worker_response_t *resp;
    while ((resp = (worker_response_t *)cq_pop(&rt->response_queue)) != NULL) {
        connection_t *conn = connection_table_get(table, resp->fd);
        if (!conn) {
            worker_response_free(resp);
            continue;
        }
        connection_prepare_response(conn, &resp->response);
        struct epoll_event ev{};
        ev.events = EPOLLOUT | EPOLLET;
        ev.data.fd = conn->fd;
        epoll_ctl(rt->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
        conn->state = CONN_STATE_WRITING;
        worker_response_free(resp);
        heap_remove_fd(&rt->connection_heap, conn->fd);
        heap_push(&rt->connection_heap, (heap_node_t){ .key_fd = conn->fd, .priority = -conn->last_activity_ms });
    }
}

static void worker_entry(void *arg) {
    worker_task_t *task = (worker_task_t *)arg;
    server_runtime_t *rt = task->runtime;
    router_result_t out;
    http_response_init(&out.response);

    router_handle_request(rt, &task->request, &out);

    worker_response_t *resp = static_cast<worker_response_t*>(std::calloc(1, sizeof(*resp)));
    resp->fd = task->fd;
    resp->response = out.response;
    out.response.body = NULL;

    cq_push(&rt->response_queue, resp);
    notify_main(rt);

    http_request_free(&task->request);
    std::free(task);
}

static void dispatch_to_pool(server_runtime_t *rt, worker_task_t *task) {
    tp_job_t job = {
        .fn = worker_entry,
        .arg = task
    };
    if (thread_pool_submit(rt->pool, job) != 0) {
        LOGE("thread pool full");
        worker_task_free(task);
    }
}

static void process_request(server_runtime_t *rt, connection_table_t *table, connection_t *conn) {
    worker_task_t *task = static_cast<worker_task_t*>(std::calloc(1, sizeof(*task)));
    task->runtime = rt;
    task->fd = conn->fd;
    task->request = conn->parser.request;
    conn->parser.request.body = NULL; // transferred
    http_parser_reset(&conn->parser);
    conn->state = CONN_STATE_PROCESSING;
    dispatch_to_pool(rt, task);
}

static void accept_new_connections(server_runtime_t *rt, connection_table_t *table) {
    while (1) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int client_fd = accept(rt->listen_fd, (struct sockaddr *)&addr, &len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            LOGE("accept error: %s", strerror(errno));
            break;
        }
        util_set_nonblocking(client_fd);
        util_set_cloexec(client_fd);
    connection_t *conn = static_cast<connection_t*>(std::calloc(1, sizeof(*conn)));
        connection_init(conn, client_fd);
        connection_table_put(table, conn);
        heap_push(&rt->connection_heap, (heap_node_t){ .key_fd = conn->fd, .priority = -conn->last_activity_ms });

        if (table->count > (size_t)rt->config.max_connections) {
            heap_node_t victim;
            if (heap_pop(&rt->connection_heap, &victim) == 0) {
                if (victim.key_fd != conn->fd) {
                    connection_table_remove(table, victim.key_fd);
                    epoll_ctl(rt->epoll_fd, EPOLL_CTL_DEL, victim.key_fd, NULL);
                }
            }
        }

        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        epoll_ctl(rt->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
    }
}

static void handle_connection_event(server_runtime_t *rt, connection_table_t *table, struct epoll_event *ev) {
    int fd = ev->data.fd;
    connection_t *conn = connection_table_get(table, fd);
    if (!conn) {
        epoll_ctl(rt->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return;
    }

    if (ev->events & (EPOLLHUP | EPOLLERR)) {
        heap_remove_fd(&rt->connection_heap, fd);
        connection_table_remove(table, fd);
        epoll_ctl(rt->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        return;
    }

    if (conn->state == CONN_STATE_READING && (ev->events & EPOLLIN)) {
        if (connection_handle_read(conn) < 0) {
            heap_remove_fd(&rt->connection_heap, fd);
            connection_table_remove(table, fd);
            epoll_ctl(rt->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            return;
        }
        heap_remove_fd(&rt->connection_heap, fd);
        heap_push(&rt->connection_heap, (heap_node_t){ .key_fd = fd, .priority = -conn->last_activity_ms });
        parse_result_t res;
        do {
            res = http_parser_execute(&conn->parser, &conn->read_buf);
            if (res == PARSE_COMPLETE) {
                process_request(rt, table, conn);
                break;
            }
            if (res == PARSE_ERROR) {
                http_response_reset(&conn->response);
                conn->response.status_code = 400;
                strcpy(conn->response.status_text, "Bad Request");
                const char *body = "{""error"":""bad_request""}";
                conn->response.body_length = strlen(body);
                conn->response.body = static_cast<char*>(std::malloc(conn->response.body_length));
                memcpy(conn->response.body, body, conn->response.body_length);
                connection_prepare_response(conn, &conn->response);
                struct epoll_event wev{};
                wev.events = EPOLLOUT | EPOLLET;
                wev.data.fd = fd;
                epoll_ctl(rt->epoll_fd, EPOLL_CTL_MOD, fd, &wev);
                break;
            }
        } while (res == PARSE_COMPLETE);
    }

    if (conn->state == CONN_STATE_WRITING && (ev->events & EPOLLOUT)) {
        if (connection_handle_write(conn) < 0 || conn->state == CONN_STATE_CLOSING) {
            heap_remove_fd(&rt->connection_heap, fd);
            connection_table_remove(table, fd);
            epoll_ctl(rt->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            return;
        }
        if (conn->state == CONN_STATE_READING) {
            struct epoll_event rev{};
            rev.events = EPOLLIN | EPOLLET;
            rev.data.fd = fd;
            epoll_ctl(rt->epoll_fd, EPOLL_CTL_MOD, fd, &rev);
        }
    }
}

int server_run(server_runtime_t *rt) {
    connection_table_t table;
    connection_table_init(&table, 1024);
    rt->connections = &table;

    rt->listen_fd = setup_listen_socket(&rt->config);
    if (rt->listen_fd < 0) {
        LOGF("failed to bind %s:%d", rt->config.listen_address, rt->config.port);
        return -1;
    }

    rt->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (rt->epoll_fd < 0) {
        LOGF("epoll_create1 failed: %s", strerror(errno));
        return -1;
    }

    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = rt->listen_fd;
    epoll_ctl(rt->epoll_fd, EPOLL_CTL_ADD, rt->listen_fd, &ev);

    rt->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    struct epoll_event eev{};
    eev.events = EPOLLIN;
    eev.data.fd = rt->event_fd;
    epoll_ctl(rt->epoll_fd, EPOLL_CTL_ADD, rt->event_fd, &eev);

    heap_init(&rt->connection_heap, rt->config.max_connections);

    router_init(rt);

    struct epoll_event events[MAX_EVENTS];

    LOGI("server listening on %s:%d", rt->config.listen_address, rt->config.port);

    while (1) {
        int n = epoll_wait(rt->epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == rt->listen_fd) {
                accept_new_connections(rt, &table);
            } else if (events[i].data.fd == rt->event_fd) {
                handle_worker_response(rt, &table);
            } else {
                handle_connection_event(rt, &table, &events[i]);
            }
        }
    }

    router_dispose();
    connection_table_free(&table);
    return 0;
}
