#include "jobs.h"

namespace mail {

WorkerTask::WorkerTask() {
    http_request_init(&request);
}

WorkerTask::~WorkerTask() {
    http_request_free(&request);
}

WorkerResponse::WorkerResponse() {
    http_response_init(&response);
}

WorkerResponse::~WorkerResponse() {
    http_response_free(&response);
}

} // namespace mail

void worker_task_free(worker_task_t *task) {
    delete task;
}

void worker_response_free(worker_response_t *resp) {
    delete resp;
}

void worker_response_dispose(void *resp) noexcept {
    worker_response_free(static_cast<worker_response_t *>(resp));
}
