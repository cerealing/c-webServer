#ifndef JOBS_H
#define JOBS_H

#include "http.h"
#include "runtime.h"

#include <memory>

namespace mail {

struct WorkerTask final {
    ServerRuntime *runtime{nullptr};
    int fd{-1};
    http_request_t request{};

    WorkerTask();
    ~WorkerTask();

    WorkerTask(const WorkerTask &) = delete;
    WorkerTask &operator=(const WorkerTask &) = delete;
    WorkerTask(WorkerTask &&) = delete;
    WorkerTask &operator=(WorkerTask &&) = delete;
};

struct WorkerResponse final {
    int fd{-1};
    http_response_t response{};

    WorkerResponse();
    ~WorkerResponse();

    WorkerResponse(const WorkerResponse &) = delete;
    WorkerResponse &operator=(const WorkerResponse &) = delete;
    WorkerResponse(WorkerResponse &&) = delete;
    WorkerResponse &operator=(WorkerResponse &&) = delete;
};

using WorkerTaskPtr = std::unique_ptr<WorkerTask>;
using WorkerResponsePtr = std::unique_ptr<WorkerResponse>;

} // namespace mail

using worker_task_t = mail::WorkerTask;
using worker_response_t = mail::WorkerResponse;

void worker_task_free(worker_task_t *task);
void worker_response_free(worker_response_t *resp);
void worker_response_dispose(void *resp) noexcept;

#endif // JOBS_H
