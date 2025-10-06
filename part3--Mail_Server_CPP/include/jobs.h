#ifndef JOBS_H
#define JOBS_H

#include "http.h"
#include "runtime.h"

typedef struct worker_task {
    server_runtime_t *runtime;
    int fd;
    http_request_t request;
} worker_task_t;

typedef struct worker_response {
    int fd;
    http_response_t response;
} worker_response_t;

void worker_task_free(worker_task_t *task);
void worker_response_free(worker_response_t *resp);

#endif // JOBS_H
