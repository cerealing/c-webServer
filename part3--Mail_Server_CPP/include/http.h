#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

typedef enum {
    HTTP_HEAD,
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_OPTIONS,
    HTTP_UNKNOWN
} http_method_t;

#define MAX_HEADERS 32

typedef struct {
    char name[64];
    char value[256];
} http_header_t;

typedef struct {
    http_method_t method;
    char path[256];
    char version[16];
    http_header_t headers[MAX_HEADERS];
    size_t header_count;
    size_t content_length;
    char *body;
} http_request_t;

typedef struct {
    int status_code;
    char status_text[64];
    http_header_t headers[MAX_HEADERS];
    size_t header_count;
    char *body;
    size_t body_length;
    int keep_alive;
} http_response_t;

int http_request_init(http_request_t *req);
void http_request_reset(http_request_t *req);
void http_request_free(http_request_t *req);

int http_response_init(http_response_t *res);
void http_response_reset(http_response_t *res);
void http_response_free(http_response_t *res);

const char *http_header_get(const http_request_t *req, const char *name);
void http_response_set_header(http_response_t *res, const char *name, const char *value);

#endif // HTTP_H
