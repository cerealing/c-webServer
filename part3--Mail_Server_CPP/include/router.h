#ifndef ROUTER_H
#define ROUTER_H

#include "http.h"

typedef struct server_runtime server_runtime_t;

typedef struct {
    http_response_t response;
} router_result_t;

void router_init(server_runtime_t *rt);
void router_dispose(void);
int router_handle_request(server_runtime_t *rt, http_request_t *req, router_result_t *out);

#endif // ROUTER_H
