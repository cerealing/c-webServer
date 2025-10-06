#include "http_parser.h"
#include "util.h"

#include <cstring>
#include <cstdlib>
#include <strings.h>
#include <cstdio>

static int parse_request_line(http_request_t *req, const char *line) {
    char method[16], path[256], version[16];
    if (sscanf(line, "%15s %255s %15s", method, path, version) != 3) {
        return -1;
    }
    req->method = HTTP_UNKNOWN;
    if (strcmp(method, "GET") == 0) req->method = HTTP_GET;
    else if (strcmp(method, "HEAD") == 0) req->method = HTTP_HEAD;
    else if (strcmp(method, "POST") == 0) req->method = HTTP_POST;
    else if (strcmp(method, "PUT") == 0) req->method = HTTP_PUT;
    else if (strcmp(method, "DELETE") == 0) req->method = HTTP_DELETE;
    else if (strcmp(method, "OPTIONS") == 0) req->method = HTTP_OPTIONS;
    util_strlcpy(req->path, sizeof(req->path), path);
    util_strlcpy(req->version, sizeof(req->version), version);
    return 0;
}

void http_parser_init(http_parser_t *parser) {
    http_request_init(&parser->request);
    parser->header_bytes = 0;
    parser->body_received = 0;
    parser->headers_complete = 0;
}

void http_parser_reset(http_parser_t *parser) {
    http_request_reset(&parser->request);
    parser->header_bytes = 0;
    parser->body_received = 0;
    parser->headers_complete = 0;
}

static char *find_double_crlf(const char *data, size_t len) {
    for (size_t i = 0; i + 3 < len; ++i) {
        if (data[i] == '\r' && data[i+1] == '\n' && data[i+2] == '\r' && data[i+3] == '\n') {
            return (char *)(data + i);
        }
    }
    return NULL;
}

parse_result_t http_parser_execute(http_parser_t *parser, byte_buffer_t *read_buf) {
    size_t readable = buffer_readable(read_buf);
    if (readable == 0) {
        return PARSE_IN_PROGRESS;
    }

    const char *data = buffer_peek(read_buf);
    if (!parser->headers_complete) {
        char *header_end = find_double_crlf(data, readable);
        if (!header_end) {
            return PARSE_IN_PROGRESS;
        }
        size_t header_len = (header_end - data) + 4;
    char *headers = static_cast<char*>(std::malloc(header_len + 1));
        memcpy(headers, data, header_len);
        headers[header_len] = '\0';

        char *saveptr;
        char *line = strtok_r(headers, "\r\n", &saveptr);
        if (!line || parse_request_line(&parser->request, line) != 0) {
            std::free(headers);
            return PARSE_ERROR;
        }

        while ((line = strtok_r(NULL, "\r\n", &saveptr))) {
            if (*line == '\0') break;
            char *colon = strchr(line, ':');
            if (!colon) continue;
            *colon = '\0';
            char *value = colon + 1;
            while (*value == ' ') value++;
            if (parser->request.header_count < MAX_HEADERS) {
        util_strlcpy(parser->request.headers[parser->request.header_count].name,
                 sizeof(parser->request.headers[parser->request.header_count].name), line);
        util_strlcpy(parser->request.headers[parser->request.header_count].value,
                 sizeof(parser->request.headers[parser->request.header_count].value), value);
                parser->request.header_count++;
            }
        }

        const char *cl = http_header_get(&parser->request, "Content-Length");
        if (cl) parser->request.content_length = (size_t)atoi(cl);
        parser->headers_complete = 1;
        parser->header_bytes = header_len;
        parser->body_received = 0;
        buffer_consume(read_buf, header_len);
    std::free(headers);
        readable = buffer_readable(read_buf);
        data = buffer_peek(read_buf);
    }

    if (parser->headers_complete) {
        size_t to_copy = parser->request.content_length - parser->body_received;
        if (to_copy > readable) to_copy = readable;
        if (to_copy > 0) {
            if (!parser->request.body) {
                parser->request.body = static_cast<char*>(std::malloc(parser->request.content_length + 1));
                parser->request.body[parser->request.content_length] = '\0';
            }
            memcpy(parser->request.body + parser->body_received, data, to_copy);
            parser->body_received += to_copy;
            buffer_consume(read_buf, to_copy);
        }
        if (parser->body_received == parser->request.content_length) {
            return PARSE_COMPLETE;
        }
    }
    return PARSE_IN_PROGRESS;
}
