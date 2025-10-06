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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static volatile int running = 1;

static void handle_sigint(int signo) {
    (void)signo;
    running = 0;
}

int main(int argc, char *argv[]) {
    const char *config_path = "config/dev_stub.json";
    if (argc > 1) config_path = argv[1];

    server_runtime_t runtime = {0};
    int rc = 1;
    int queue_ready = 0;
    config_load(config_path, &runtime.config);

    if (logger_init(runtime.config.log_path) != 0) {
        fprintf(stderr, "Failed to open log file %s\n", runtime.config.log_path);
    }
    logger_set_level(LOG_DEBUG);

    thread_pool_config_t pool_cfg{};
    pool_cfg.thread_count = static_cast<size_t>(runtime.config.thread_pool_size);
    pool_cfg.queue_capacity = static_cast<size_t>(runtime.config.thread_pool_size) * 4;
    pool_cfg.on_error = NULL;

    runtime.pool = thread_pool_create(&pool_cfg);
    if (!runtime.pool) {
        LOGF("failed to create thread pool");
        goto cleanup;
    }

    if (cq_init(&runtime.response_queue) != 0) {
        LOGF("failed to init response queue");
        goto cleanup;
    }
    queue_ready = 1;

    if (db_init(&runtime.config, &runtime.db) != 0) {
        LOGF("failed to initialize database backend");
        goto cleanup;
    }

    runtime.auth = auth_service_create(runtime.db);
    runtime.mail = mail_service_create(runtime.db, &runtime.config);
    runtime.templates = template_engine_create(runtime.config.template_dir);
    if (!runtime.auth || !runtime.mail || !runtime.templates) {
        LOGF("failed to initialize services");
        goto cleanup;
    }

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    rc = server_run(&runtime);

cleanup:
    template_engine_destroy(runtime.templates);
    mail_service_destroy(runtime.mail);
    auth_service_destroy(runtime.auth);
    if (runtime.db) db_close(runtime.db);
    if (runtime.pool) thread_pool_destroy(runtime.pool);
    if (queue_ready) {
        cq_destroy(&runtime.response_queue, (void (*)(void *))worker_response_free);
    }
    logger_close();
    return rc;
}
