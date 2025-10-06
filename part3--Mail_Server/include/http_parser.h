#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include "http.h"
#include "buffer.h"

typedef enum {
    PARSE_IN_PROGRESS,
    PARSE_COMPLETE,
    PARSE_ERROR
} parse_result_t;

typedef struct {
    http_request_t request;
    size_t header_bytes;
    size_t body_received;
    int headers_complete;
} http_parser_t;

void http_parser_init(http_parser_t *parser);
void http_parser_reset(http_parser_t *parser);
parse_result_t http_parser_execute(http_parser_t *parser, byte_buffer_t *read_buf);

#endif // HTTP_PARSER_H
