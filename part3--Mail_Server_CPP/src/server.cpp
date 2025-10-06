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
#include <memory>
#include <vector>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <netinet/tcp.h>

#define MAX_EVENTS 128

namespace mail {

struct ConnectionDeleter {
    void operator()(connection_t *conn) const noexcept {
        if (conn) {
            connection_free(conn);
            delete conn;
        }
    }
};

using ConnectionHandle = std::unique_ptr<connection_t, ConnectionDeleter>;

class ConnectionTable {
public:
    explicit ConnectionTable(std::size_t initial_capacity)
        : slots_(initial_capacity), count_(0) {}

    connection_t *get(int fd) noexcept {
        if (fd < 0) return nullptr;
        const auto idx = static_cast<std::size_t>(fd);
        if (idx >= slots_.size()) return nullptr;
        return slots_[idx].get();
    }

    void insert(ConnectionHandle conn) {
        const auto fd = static_cast<std::size_t>(conn->fd);
        ensure_capacity(fd + 1);
        if (!slots_[fd]) {
            ++count_;
        }
        slots_[fd] = std::move(conn);
    }

    void erase(int fd) noexcept {
        if (fd < 0) return;
        const auto idx = static_cast<std::size_t>(fd);
        if (idx >= slots_.size()) return;
        if (slots_[idx]) {
            slots_[idx].reset();
            if (count_ > 0) {
                --count_;
            }
        }
    }

    std::size_t size() const noexcept { return count_; }

    void clear() noexcept {
        for (auto &slot : slots_) {
            slot.reset();
        }
        count_ = 0;
    }

private:
    void ensure_capacity(std::size_t desired) {
        if (desired <= slots_.size()) {
            return;
        }
        std::size_t new_cap = slots_.empty() ? 1024 : slots_.size();
        while (new_cap < desired) {
            new_cap *= 2;
        }
        slots_.resize(new_cap);
    }

    std::vector<ConnectionHandle> slots_;
    std::size_t count_;
};

inline ConnectionHandle make_connection(int fd) {
    auto conn = ConnectionHandle{new connection_t{}, ConnectionDeleter{}};
    connection_init(conn.get(), fd);
    return conn;
}

} // namespace mail

namespace mail {
namespace {

int setup_listen_socket(const ServerConfig &cfg) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cfg.port);
    addr.sin_addr.s_addr = inet_addr(cfg.listen_address.c_str());

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, static_cast<int>(cfg.max_connections)) < 0) {
        close(fd);
        return -1;
    }
    util_set_nonblocking(fd);
    util_set_cloexec(fd);
    return fd;
}

void notify_main(ServerRuntime *rt) {
    uint64_t one = 1;
    ssize_t written = write(rt->event_fd, &one, sizeof(one));
    (void)written;
}

void handle_worker_response(ServerRuntime *rt, ConnectionTable &table) {
    uint64_t val;
    while (read(rt->event_fd, &val, sizeof(val)) > 0) {}

    while (auto *raw = static_cast<worker_response_t *>(cq_pop(&rt->response_queue))) {
        std::unique_ptr<worker_response_t> resp(raw);
        connection_t *conn = table.get(resp->fd);
        if (!conn) {
            continue;
        }
        connection_prepare_response(conn, &resp->response);
        struct epoll_event ev{};
        ev.events = EPOLLOUT | EPOLLET;
        ev.data.fd = conn->fd;
        epoll_ctl(rt->epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev);
        conn->state = CONN_STATE_WRITING;
        heap_remove_fd(&rt->connection_heap, conn->fd);
        heap_push(&rt->connection_heap, heap_node_t{ .key_fd = conn->fd, .priority = -conn->last_activity_ms });
    }
}

void worker_entry(void *arg) {
    std::unique_ptr<worker_task_t> task(static_cast<worker_task_t *>(arg));
    ServerRuntime *rt = task->runtime;
    RouterResult out;
    http_response_init(&out.response);

    router_handle_request(rt, &task->request, &out);

    auto resp = std::make_unique<worker_response_t>();
    resp->fd = task->fd;
    resp->response = out.response;
    out.response.body = NULL;

    cq_push(&rt->response_queue, resp.release());
    notify_main(rt);
}

bool dispatch_to_pool(ServerRuntime *rt, std::unique_ptr<worker_task_t> task) {
    tp_job_t job = {
        .fn = worker_entry,
        .arg = task.get()
    };
    if (thread_pool_submit(rt->pool, job) != 0) {
        LOGE("thread pool full");
        return false;
    }
    task.release();
    return true;
}

void process_request(ServerRuntime *rt, connection_t *conn) {
    auto task = std::make_unique<worker_task_t>();
    task->runtime = rt;
    task->fd = conn->fd;
    task->request = conn->parser.request;
    conn->parser.request.body = NULL; // transferred
    http_parser_reset(&conn->parser);
    conn->state = CONN_STATE_PROCESSING;
    if (!dispatch_to_pool(rt, std::move(task))) {
        conn->state = CONN_STATE_READING;
    }
}

void accept_new_connections(ServerRuntime *rt, ConnectionTable &table) {
    while (true) {
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

        auto conn_handle = make_connection(client_fd);
        connection_t *conn = conn_handle.get();
        table.insert(std::move(conn_handle));
        heap_push(&rt->connection_heap, heap_node_t{ .key_fd = conn->fd, .priority = -conn->last_activity_ms });

        if (table.size() > rt->config.max_connections) {
            heap_node_t victim;
            if (heap_pop(&rt->connection_heap, &victim) == 0) {
                if (victim.key_fd != conn->fd) {
                    connection_t *drop = table.get(victim.key_fd);
                    if (drop) {
                        epoll_ctl(rt->epoll_fd, EPOLL_CTL_DEL, victim.key_fd, NULL);
                        table.erase(victim.key_fd);
                    }
                }
            }
        }

        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        epoll_ctl(rt->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
    }
}

void handle_connection_event(ServerRuntime *rt, ConnectionTable &table, struct epoll_event *ev) {
    int fd = ev->data.fd;
    connection_t *conn = table.get(fd);
    if (!conn) {
        epoll_ctl(rt->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return;
    }

    if (ev->events & (EPOLLHUP | EPOLLERR)) {
        heap_remove_fd(&rt->connection_heap, fd);
        epoll_ctl(rt->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
        table.erase(fd);
        return;
    }

    if (conn->state == CONN_STATE_READING && (ev->events & EPOLLIN)) {
        if (connection_handle_read(conn) < 0) {
            heap_remove_fd(&rt->connection_heap, fd);
            epoll_ctl(rt->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            table.erase(fd);
            return;
        }
        heap_remove_fd(&rt->connection_heap, fd);
        heap_push(&rt->connection_heap, heap_node_t{ .key_fd = fd, .priority = -conn->last_activity_ms });
        parse_result_t res;
        do {
            res = http_parser_execute(&conn->parser, &conn->read_buf);
            if (res == PARSE_COMPLETE) {
                process_request(rt, conn);
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
            epoll_ctl(rt->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            table.erase(fd);
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

} // namespace

int server_run(ServerRuntime *rt) {
    ConnectionTable table{1024};
    rt->connections = &table;

    rt->listen_fd = setup_listen_socket(rt->config);
    if (rt->listen_fd < 0) {
        LOGF("failed to bind %s:%d", rt->config.listen_address.c_str(), rt->config.port);
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

    LOGI("server listening on %s:%d", rt->config.listen_address.c_str(), rt->config.port);

    while (1) {
        int n = epoll_wait(rt->epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == rt->listen_fd) {
                accept_new_connections(rt, table);
            } else if (events[i].data.fd == rt->event_fd) {
                handle_worker_response(rt, table);
            } else {
                handle_connection_event(rt, table, &events[i]);
            }
        }
    }

    router_dispose();
    table.clear();
    rt->connections = nullptr;
    return 0;
}

} // namespace mail
