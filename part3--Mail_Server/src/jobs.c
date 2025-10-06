#include "jobs.h"

#include <stdlib.h>

void worker_task_free(worker_task_t *task) {
    if (!task) return;
    http_request_free(&task->request);
    free(task);
}

void worker_response_free(worker_response_t *resp) {
    if (!resp) return;
    http_response_free(&resp->response);
    free(resp);
}
