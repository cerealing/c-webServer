#include "http.h"

#include <stdlib.h>
#include <string.h>

int http_request_init(http_request_t *req) {
    memset(req, 0, sizeof(*req));
    req->method = HTTP_UNKNOWN;
    req->body = NULL;
    return 0;
}

void http_request_reset(http_request_t *req) {
    for (size_t i = 0; i < req->header_count; ++i) {
        req->headers[i].name[0] = '\0';
        req->headers[i].value[0] = '\0';
    }
    req->header_count = 0;
    req->content_length = 0;
    if (req->body) {
        free(req->body);
        req->body = NULL;
    }
    req->method = HTTP_UNKNOWN;
    req->path[0] = '\0';
    strcpy(req->version, "HTTP/1.1");
}

void http_request_free(http_request_t *req) {
    if (req->body) free(req->body);
    req->body = NULL;
}

int http_response_init(http_response_t *res) {
    memset(res, 0, sizeof(*res));
    res->status_code = 200;
    strcpy(res->status_text, "OK");
    res->keep_alive = 1;
    return 0;
}

void http_response_reset(http_response_t *res) {
    for (size_t i = 0; i < res->header_count; ++i) {
        res->headers[i].name[0] = '\0';
        res->headers[i].value[0] = '\0';
    }
    res->header_count = 0;
    if (res->body) {
        free(res->body);
        res->body = NULL;
    }
    res->body_length = 0;
    res->status_code = 200;
    strcpy(res->status_text, "OK");
    res->keep_alive = 1;
}

void http_response_free(http_response_t *res) {
    if (res->body) free(res->body);
    res->body = NULL;
}

const char *http_header_get(const http_request_t *req, const char *name) {
    for (size_t i = 0; i < req->header_count; ++i) {
        if (strcasecmp(req->headers[i].name, name) == 0) {
            return req->headers[i].value;
        }
    }
    return NULL;
}

void http_response_set_header(http_response_t *res, const char *name, const char *value) {
    for (size_t i = 0; i < res->header_count; ++i) {
        if (strcasecmp(res->headers[i].name, name) == 0) {
            strncpy(res->headers[i].value, value, sizeof(res->headers[i].value)-1);
            res->headers[i].value[sizeof(res->headers[i].value)-1] = '\0';
            return;
        }
    }
    if (res->header_count < MAX_HEADERS) {
        strncpy(res->headers[res->header_count].name, name, sizeof(res->headers[res->header_count].name)-1);
        res->headers[res->header_count].name[sizeof(res->headers[res->header_count].name)-1] = '\0';
        strncpy(res->headers[res->header_count].value, value, sizeof(res->headers[res->header_count].value)-1);
        res->headers[res->header_count].value[sizeof(res->headers[res->header_count].value)-1] = '\0';
        res->header_count++;
    }
}
