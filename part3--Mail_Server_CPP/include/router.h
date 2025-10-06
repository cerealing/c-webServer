#ifndef ROUTER_H
#define ROUTER_H

#include "http.h"

namespace mail {

struct ServerRuntime;

struct RouterResult {
    http_response_t response{};
};

void router_init(ServerRuntime *rt);
void router_dispose();
int router_handle_request(ServerRuntime *rt, http_request_t *req, RouterResult *out);

} // namespace mail

using router_result_t = mail::RouterResult;

#endif // ROUTER_H
