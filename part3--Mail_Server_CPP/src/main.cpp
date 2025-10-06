#include "config.h"
#include "runtime.h"
#include "logger.h"
#include "thread_pool.h"
#include "server.h"
#include "jobs.h"
#include "services/auth_service.h"
#include "services/mail_service.h"
#include "template_engine.h"
#include "db.h"

#include <csignal>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {

struct LoggerGuard {
    ~LoggerGuard() { logger_close(); }
};

struct ResponseQueueGuard {
    explicit ResponseQueueGuard(concurrent_queue_t &queue)
        : queue_(queue), initialized_(cq_init(&queue_) == 0) {}

    ResponseQueueGuard(const ResponseQueueGuard &) = delete;
    ResponseQueueGuard &operator=(const ResponseQueueGuard &) = delete;

    ~ResponseQueueGuard() {
        if (initialized_) {
            cq_destroy(&queue_, worker_response_dispose);
        }
    }

    bool ok() const noexcept { return initialized_; }

private:
    concurrent_queue_t &queue_;
    bool initialized_;
};

void handle_sigint(int signo) {
    (void)signo;
}

} // namespace

int main(int argc, char *argv[]) {
    std::filesystem::path config_path = "config/dev_stub.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    mail::ServerRuntime runtime{};
    mail::load_config(config_path, runtime.config);

    std::fprintf(stderr, "[maild] config loaded from %s\n", config_path.string().c_str());

    const std::string log_target = runtime.config.log_target();
    if (logger_init(log_target.c_str()) != 0) {
        std::fprintf(stderr, "Failed to open log file %s\n", log_target.c_str());
    }
    LOGI("logger initialized (target=%s)", log_target.c_str());
    logger_set_level(LOG_DEBUG);
    LoggerGuard logger_guard;

    thread_pool_config_t pool_cfg{};
    pool_cfg.thread_count = runtime.config.thread_pool_size;
    pool_cfg.queue_capacity = runtime.config.thread_pool_size * 4;
    pool_cfg.on_error = NULL;

    using ThreadPoolPtr = std::unique_ptr<thread_pool_t, decltype(&thread_pool_destroy)>;
    ThreadPoolPtr pool(thread_pool_create(&pool_cfg), thread_pool_destroy);
    if (!pool) {
        LOGF("failed to create thread pool");
        return 1;
    }
    runtime.pool = pool.get();
    LOGI("thread pool ready with %zu threads", runtime.config.thread_pool_size);

    ResponseQueueGuard response_queue_guard(runtime.response_queue);
    if (!response_queue_guard.ok()) {
        LOGF("failed to init response queue");
        return 1;
    }
    LOGI("response queue initialized");

    db_handle_t *db_raw = nullptr;
    if (db_init(runtime.config, &db_raw) != 0) {
        LOGF("failed to initialize database backend");
        return 1;
    }
    using DbPtr = std::unique_ptr<db_handle_t, decltype(&db_close)>;
    DbPtr db(db_raw, db_close);
    runtime.db = db.get();
    LOGI("database backend ready");

    using AuthPtr = std::unique_ptr<auth_context_t, decltype(&auth_service_destroy)>;
    using MailPtr = std::unique_ptr<mail_service_t, decltype(&mail_service_destroy)>;
    using TemplatePtr = std::unique_ptr<template_engine_t, decltype(&template_engine_destroy)>;

    AuthPtr auth(auth_service_create(runtime.db), auth_service_destroy);
    MailPtr mail(mail_service_create(runtime.db, runtime.config), mail_service_destroy);
    TemplatePtr templates(template_engine_create(runtime.config.template_dir.string().c_str()), template_engine_destroy);

    if (!auth || !mail || !templates) {
        LOGF("failed to initialize services");
        return 1;
    }
    runtime.auth = auth.get();
    runtime.mail = mail.get();
    runtime.templates = templates.get();
    LOGI("auth/mail/template services initialized");

    std::signal(SIGINT, handle_sigint);
    std::signal(SIGTERM, handle_sigint);
    LOGI("signal handlers registered");

    const int rc = mail::server_run(&runtime);

    LOGI("server_run exited with code %d", rc);

    runtime.templates = nullptr;
    runtime.mail = nullptr;
    runtime.auth = nullptr;
    runtime.db = nullptr;
    runtime.pool = nullptr;

    return rc;
}
