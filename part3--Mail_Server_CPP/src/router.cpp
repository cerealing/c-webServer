#include "router.h"

#include "runtime.h"
#include "logger.h"
#include "services/auth_service.h"
#include "services/mail_service.h"
#include "template_engine.h"
#include "jsmn.h"
#include "util.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <ctype.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <errno.h>
#include <strings.h>

#define JSON_TOKEN_COUNT 768

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} json_writer_t;

static int jw_reserve(json_writer_t *jw, size_t extra) {
    if (jw->len + extra + 1 <= jw->cap) return 0;
    size_t new_cap = jw->cap == 0 ? 1024 : jw->cap;
    while (jw->len + extra + 1 > new_cap) new_cap *= 2;
    char *tmp = static_cast<char*>(std::realloc(jw->buf, new_cap));
    if (!tmp) return -1;
    jw->buf = tmp;
    jw->cap = new_cap;
    return 0;
}

static int jw_append_len(json_writer_t *jw, const char *data, size_t len) {
    if (jw_reserve(jw, len) != 0) return -1;
    memcpy(jw->buf + jw->len, data, len);
    jw->len += len;
    jw->buf[jw->len] = '\0';
    return 0;
}

static int jw_append(json_writer_t *jw, const char *data) {
    if (!data) data = "";
    return jw_append_len(jw, data, strlen(data));
}

static int jw_append_char(json_writer_t *jw, char c) {
    if (jw_reserve(jw, 1) != 0) return -1;
    jw->buf[jw->len++] = c;
    jw->buf[jw->len] = '\0';
    return 0;
}

static int jw_appendf(json_writer_t *jw, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (needed < 0) {
        va_end(ap);
        return -1;
    }
    if (jw_reserve(jw, (size_t)needed) != 0) {
        va_end(ap);
        return -1;
    }
    vsnprintf(jw->buf + jw->len, jw->cap - jw->len, fmt, ap);
    jw->len += (size_t)needed;
    jw->buf[jw->len] = '\0';
    va_end(ap);
    return 0;
}

static int jw_append_json_string(json_writer_t *jw, const char *value) {
    if (!value) value = "";
    if (jw_append_char(jw, '"') != 0) return -1;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        unsigned char c = *p;
        switch (c) {
            case '\\': case '"':
                if (jw_append_len(jw, "\\", 1) != 0) return -1;
                if (jw_append_char(jw, c) != 0) return -1;
                break;
            case '\b':
                if (jw_append_len(jw, "\\b", 2) != 0) return -1;
                break;
            case '\f':
                if (jw_append_len(jw, "\\f", 2) != 0) return -1;
                break;
            case '\n':
                if (jw_append_len(jw, "\\n", 2) != 0) return -1;
                break;
            case '\r':
                if (jw_append_len(jw, "\\r", 2) != 0) return -1;
                break;
            case '\t':
                if (jw_append_len(jw, "\\t", 2) != 0) return -1;
                break;
            default:
                if (c < 0x20) {
                    if (jw_appendf(jw, "\\u%04x", c) != 0) return -1;
                } else {
                    if (jw_append_char(jw, (char)c) != 0) return -1;
                }
        }
    }
    return jw_append_char(jw, '"');
}

static char *jw_detach(json_writer_t *jw, size_t *length) {
    if (!jw->buf) {
    jw->buf = static_cast<char*>(std::calloc(1, 1));
        jw->cap = 1;
    }
    char *out = jw->buf;
    if (length) *length = jw->len;
    jw->buf = NULL;
    jw->len = jw->cap = 0;
    return out;
}

static void set_common_headers(http_response_t *res) {
    http_response_set_header(res, "Server", "MailServer/0.1");
    http_response_set_header(res, "Access-Control-Allow-Origin", "*");
    http_response_set_header(res, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    http_response_set_header(res, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
}

static void respond_with_json_writer(http_response_t *res, int status_code, const char *status_text, json_writer_t *jw) {
    set_common_headers(res);
    http_response_set_header(res, "Content-Type", "application/json; charset=utf-8");
    res->status_code = status_code;
    strncpy(res->status_text, status_text, sizeof(res->status_text) - 1);
    res->status_text[sizeof(res->status_text) - 1] = '\0';
    res->body = jw_detach(jw, &res->body_length);
}

static void respond_with_error(http_response_t *res, int status_code, const char *code, const char *message) {
    json_writer_t jw = {0};
    jw_append(&jw, "{\"error\":{");
    jw_append(&jw, "\"code\":");
    jw_append_json_string(&jw, code);
    jw_append(&jw, ",\"message\":");
    jw_append_json_string(&jw, message);
    jw_append(&jw, "}}}");
    respond_with_json_writer(res, status_code, "Error", &jw);
}

static const char *folder_kind_to_string(folder_kind_t kind) {
    switch (kind) {
        case FOLDER_INBOX: return "inbox";
        case FOLDER_SENT: return "sent";
        case FOLDER_DRAFTS: return "drafts";
        case FOLDER_STARRED: return "starred";
        case FOLDER_ARCHIVE: return "archive";
        case FOLDER_CUSTOM: return "custom";
        default: return "custom";
    }
}

static int folder_kind_from_string(const char *s, folder_kind_t *out) {
    if (!s || !out) return -1;
    if (strcasecmp(s, "inbox") == 0) *out = FOLDER_INBOX;
    else if (strcasecmp(s, "sent") == 0) *out = FOLDER_SENT;
    else if (strcasecmp(s, "drafts") == 0) *out = FOLDER_DRAFTS;
    else if (strcasecmp(s, "starred") == 0) *out = FOLDER_STARRED;
    else if (strcasecmp(s, "archive") == 0) *out = FOLDER_ARCHIVE;
    else if (strcasecmp(s, "custom") == 0) *out = FOLDER_CUSTOM;
    else return -1;
    return 0;
}

static int json_parse(const char *json, jsmntok_t **tokens_out, int *count_out) {
    if (!json) return -1;
    jsmn_parser parser;
    jsmn_init(&parser);
    jsmntok_t *tokens = static_cast<jsmntok_t*>(std::calloc(JSON_TOKEN_COUNT, sizeof(jsmntok_t)));
    if (!tokens) return -1;
    int r = jsmn_parse(&parser, json, (unsigned int)strlen(json), tokens, JSON_TOKEN_COUNT);
    if (r < 0) {
    std::free(tokens);
        return -1;
    }
    *tokens_out = tokens;
    if (count_out) *count_out = r;
    return 0;
}

static int json_token_equals(const char *json, const jsmntok_t *tok, const char *s) {
    if (!json || !tok || !s) return 0;
    int len = tok->end - tok->start;
    return (int)strlen(s) == len && strncmp(json + tok->start, s, len) == 0;
}

static int json_find_field(const char *json, jsmntok_t *tokens, int tok_count, const char *key) {
    for (int i = 1; i < tok_count; ++i) {
        if (tokens[i].type == JSMN_STRING && tokens[i].parent == 0) {
            if (json_token_equals(json, &tokens[i], key)) {
                return i + 1;
            }
        }
    }
    return -1;
}

static int json_copy_string(const char *json, const jsmntok_t *tok, char *out, size_t out_sz) {
    if (!json || !tok || !out || out_sz == 0) return -1;
    int len = tok->end - tok->start;
    if (len < 0) return -1;
    if ((size_t)len >= out_sz) len = (int)out_sz - 1;
    memcpy(out, json + tok->start, (size_t)len);
    out[len] = '\0';
    return 0;
}

static int json_get_bool(const char *json, const jsmntok_t *tok, int *out) {
    if (!json || !tok || !out) return -1;
    if (tok->type != JSMN_PRIMITIVE) return -1;
    if (tok->end - tok->start == 4 && strncmp(json + tok->start, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (tok->end - tok->start == 5 && strncmp(json + tok->start, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static int json_get_long(const char *json, const jsmntok_t *tok, long long *out) {
    if (!json || !tok || !out) return -1;
    if (tok->type != JSMN_PRIMITIVE && tok->type != JSMN_STRING) return -1;
    int len = tok->end - tok->start;
    char buf[64];
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, json + tok->start, (size_t)len);
    buf[len] = '\0';
    char *endptr = NULL;
    long long val = strtoll(buf, &endptr, 10);
    if (!endptr || *endptr != '\0') return -1;
    *out = val;
    return 0;
}

static void respond_with_template(server_runtime_t *rt, http_response_t *res, const char *name,
                                  const template_var_t *vars, size_t var_count) {
    char *html = NULL;
    size_t len = 0;
    if (template_engine_render(rt->templates, name, vars, var_count, &html, &len) != 0) {
        respond_with_error(res, 500, "template_error", "Failed to render template");
        return;
    }
    set_common_headers(res);
    http_response_set_header(res, "Content-Type", "text/html; charset=utf-8");
    res->status_code = 200;
    strncpy(res->status_text, "OK", sizeof(res->status_text) - 1);
    res->status_text[sizeof(res->status_text) - 1] = '\0';
    res->body = html;
    res->body_length = len;
}

static int is_safe_segment(const char *s) {
    return !(strcmp(s, "..") == 0 || strchr(s, '\\'));
}

static int build_safe_path(const char *root, const char *rel, char *out, size_t out_len) {
    if (!root || !rel || !out) return -1;
    char decoded[512];
    size_t di = 0;
    for (size_t i = 0; rel[i] && di + 1 < sizeof(decoded); ++i) {
        if (rel[i] == '%' && rel[i+1] && rel[i+2]) {
            char hex[3] = { rel[i+1], rel[i+2], '\0' };
            decoded[di++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (rel[i] == '+') {
            decoded[di++] = ' ';
        } else {
            decoded[di++] = rel[i];
        }
    }
    decoded[di] = '\0';

    char tmp[512];
    strncpy(tmp, decoded, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *saveptr;
    char *segment = strtok_r(tmp, "/", &saveptr);
    char path[768];
    snprintf(path, sizeof(path), "%s", root);
    size_t path_len = strlen(path);
    if (path[path_len - 1] != '/') {
        path[path_len++] = '/';
        path[path_len] = '\0';
    }

    while (segment) {
        if (!is_safe_segment(segment)) return -1;
        size_t seg_len = strlen(segment);
        if (path_len + seg_len + 2 >= sizeof(path)) return -1;
        memcpy(path + path_len, segment, seg_len);
        path_len += seg_len;
        path[path_len++] = '/';
        path[path_len] = '\0';
        segment = strtok_r(NULL, "/", &saveptr);
    }

    if (path_len > 0 && path[path_len - 1] == '/') {
        path[--path_len] = '\0';
    }
    if (strlen(path) >= out_len) return -1;
    strcpy(out, path);
    return 0;
}

static int read_file_all(const char *path, char **out_buf, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return -1;
    }
    rewind(fp);
    char *buf = static_cast<char*>(std::malloc((size_t)sz + 1));
    if (!buf) {
        fclose(fp);
        return -1;
    }
    size_t read = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (read != (size_t)sz) {
    std::free(buf);
        return -1;
    }
    buf[sz] = '\0';
    *out_buf = buf;
    if (out_len) *out_len = (size_t)sz;
    return 0;
}

static const char *mime_from_path(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    ext++;
    if (strcasecmp(ext, "html") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(ext, "css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(ext, "js") == 0) return "application/javascript; charset=utf-8";
    if (strcasecmp(ext, "json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(ext, "png") == 0) return "image/png";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, "svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, "gif") == 0) return "image/gif";
    return "application/octet-stream";
}

static void respond_with_static(server_runtime_t *rt, http_response_t *res, const char *rel_path) {
    char effective[512];
    if (!rel_path || rel_path[0] == '\0') {
        strcpy(effective, "index.html");
    } else {
        strncpy(effective, rel_path, sizeof(effective) - 1);
        effective[sizeof(effective) - 1] = '\0';
        size_t elen = strlen(effective);
        if (elen > 0 && effective[elen - 1] == '/') {
            if (elen + strlen("index.html") >= sizeof(effective)) {
                respond_with_error(res, 400, "bad_path", "Static path too long");
                return;
            }
            strcat(effective, "index.html");
        }
    }
    char fullpath[1024];
    if (build_safe_path(rt->config.static_dir, effective, fullpath, sizeof(fullpath)) != 0) {
        respond_with_error(res, 400, "bad_path", "Invalid static path");
        return;
    }
    char *data = NULL;
    size_t len = 0;
    if (read_file_all(fullpath, &data, &len) != 0) {
        respond_with_error(res, 404, "not_found", "Static asset not found");
        return;
    }
    set_common_headers(res);
    http_response_set_header(res, "Content-Type", mime_from_path(fullpath));
    res->status_code = 200;
    strncpy(res->status_text, "OK", sizeof(res->status_text) - 1);
    res->status_text[sizeof(res->status_text) - 1] = '\0';
    res->body = data;
    res->body_length = len;
}

static int extract_bearer_token(const http_request_t *req, char *out, size_t out_len) {
    const char *auth = http_header_get(req, "Authorization");
    if (!auth) return -1;
    while (*auth == ' ') auth++;
    if (strncasecmp(auth, "Bearer", 6) == 0) {
        auth += 6;
        if (*auth == ' ') auth++;
    }
    size_t len = strlen(auth);
    if (len == 0 || len >= out_len) return -1;
    strncpy(out, auth, out_len - 1);
    out[out_len - 1] = '\0';
    return 0;
}

static void respond_unauthorized(http_response_t *res) {
    respond_with_error(res, 401, "unauthorized", "Authentication required");
    http_response_set_header(res, "WWW-Authenticate", "Bearer realm=\"mail\"");
}

static int ensure_authenticated(server_runtime_t *rt, const http_request_t *req, http_response_t *res,
                                user_record_t *user_out, char *token_buf, size_t token_len) {
    if (extract_bearer_token(req, token_buf, token_len) != 0) {
        respond_unauthorized(res);
        return -1;
    }
    if (auth_service_validate(rt->auth, token_buf, user_out) != 0) {
        respond_unauthorized(res);
        return -1;
    }
    return 0;
}

static void json_write_user(json_writer_t *jw, const user_record_t *user) {
    jw_append(jw, "{");
    jw_append(jw, "\"id\":");
    jw_appendf(jw, "%llu", (unsigned long long)user->id);
    jw_append(jw, ",\"username\":");
    jw_append_json_string(jw, user->username);
    jw_append(jw, ",\"email\":");
    jw_append_json_string(jw, user->email);
    jw_append(jw, ",\"createdAt\":");
    jw_appendf(jw, "%lld", (long long)user->created_at);
    jw_append(jw, "}");
}

static void json_write_folder(json_writer_t *jw, const folder_record_t *folder) {
    jw_append(jw, "{");
    jw_append(jw, "\"id\":");
    jw_appendf(jw, "%llu", (unsigned long long)folder->id);
    jw_append(jw, ",\"name\":");
    jw_append_json_string(jw, folder->name);
    jw_append(jw, ",\"kind\":");
    jw_append_json_string(jw, folder_kind_to_string(folder->kind));
    jw_append(jw, ",\"createdAt\":");
    jw_appendf(jw, "%lld", (long long)folder->created_at);
    jw_append(jw, "}");
}

static void json_write_folder_list(json_writer_t *jw, const folder_list_t *list) {
    jw_append(jw, "[");
    for (size_t i = 0; i < list->count; ++i) {
        if (i > 0) jw_append_char(jw, ',');
        json_write_folder(jw, &list->items[i]);
    }
    jw_append(jw, "]");
}

static void json_write_message(json_writer_t *jw, const message_record_t *msg) {
    jw_append(jw, "{");
    jw_append(jw, "\"id\":");
    jw_appendf(jw, "%llu", (unsigned long long)msg->id);
    jw_append(jw, ",\"ownerId\":");
    jw_appendf(jw, "%llu", (unsigned long long)msg->owner_id);
    jw_append(jw, ",\"folder\":");
    jw_append_json_string(jw, folder_kind_to_string(msg->folder));
    jw_append(jw, ",\"customFolder\":");
    jw_append_json_string(jw, msg->custom_folder);
    jw_append(jw, ",\"archiveGroup\":");
    jw_append_json_string(jw, msg->archive_group);
    jw_append(jw, ",\"subject\":");
    jw_append_json_string(jw, msg->subject);
    jw_append(jw, ",\"body\":");
    jw_append_json_string(jw, msg->body);
    jw_append(jw, ",\"recipients\":");
    jw_append_json_string(jw, msg->recipients);
    jw_append(jw, ",\"isStarred\":");
    jw_append(jw, msg->is_starred ? "true" : "false");
    jw_append(jw, ",\"isDraft\":");
    jw_append(jw, msg->is_draft ? "true" : "false");
    jw_append(jw, ",\"isArchived\":");
    jw_append(jw, msg->is_archived ? "true" : "false");
    jw_append(jw, ",\"createdAt\":");
    jw_appendf(jw, "%lld", (long long)msg->created_at);
    jw_append(jw, ",\"updatedAt\":");
    jw_appendf(jw, "%lld", (long long)msg->updated_at);
    jw_append(jw, "}");
}

static void json_write_message_list(json_writer_t *jw, const message_list_t *list) {
    jw_append(jw, "[");
    for (size_t i = 0; i < list->count; ++i) {
        if (i > 0) jw_append_char(jw, ',');
        json_write_message(jw, &list->items[i]);
    }
    jw_append(jw, "]");
}

static void json_write_attachment_list(json_writer_t *jw, const attachment_list_t *list) {
    jw_append(jw, "[");
    for (size_t i = 0; i < list->count; ++i) {
        if (i > 0) jw_append_char(jw, ',');
        const attachment_record_t *att = &list->items[i];
        jw_append(jw, "{");
        jw_append(jw, "\"id\":");
        jw_appendf(jw, "%llu", (unsigned long long)att->id);
        jw_append(jw, ",\"messageId\":");
        jw_appendf(jw, "%llu", (unsigned long long)att->message_id);
        jw_append(jw, ",\"filename\":");
        jw_append_json_string(jw, att->filename);
        jw_append(jw, ",\"storagePath\":");
        jw_append_json_string(jw, att->storage_path);
        jw_append(jw, ",\"relativePath\":");
        jw_append_json_string(jw, att->relative_path);
        jw_append(jw, ",\"mimeType\":");
        jw_append_json_string(jw, att->mime_type);
        jw_append(jw, ",\"sizeBytes\":");
        jw_appendf(jw, "%llu", (unsigned long long)att->size_bytes);
        jw_append(jw, "}");
    }
    jw_append(jw, "]");
}

static void json_write_contact(json_writer_t *jw, const contact_record_t *c) {
    jw_append(jw, "{");
    jw_append(jw, "\"id\":");
    jw_appendf(jw, "%llu", (unsigned long long)c->id);
    jw_append(jw, ",\"alias\":");
    jw_append_json_string(jw, c->alias);
    jw_append(jw, ",\"contactUserId\":");
    jw_appendf(jw, "%llu", (unsigned long long)c->contact_user_id);
    jw_append(jw, ",\"groupName\":");
    jw_append_json_string(jw, c->group_name);
    jw_append(jw, ",\"createdAt\":");
    jw_appendf(jw, "%lld", (long long)c->created_at);
    jw_append(jw, "}");
}

static void json_write_contact_list(json_writer_t *jw, const contact_list_t *list) {
    jw_append(jw, "[");
    for (size_t i = 0; i < list->count; ++i) {
        if (i > 0) jw_append_char(jw, ',');
        json_write_contact(jw, &list->items[i]);
    }
    jw_append(jw, "]");
}

static void split_path_query(const char *full, char *path_out, size_t path_len, const char **query_out) {
    if (!full || !path_out || path_len == 0) {
        if (path_out && path_len > 0) path_out[0] = '\0';
        if (query_out) *query_out = NULL;
        return;
    }
    const char *q = strchr(full, '?');
    if (!q) {
    util_strlcpy(path_out, path_len, full);
        if (query_out) *query_out = NULL;
        return;
    }
    size_t len = (size_t)(q - full);
    if (len >= path_len) len = path_len - 1;
    memcpy(path_out, full, len);
    path_out[len] = '\0';
    if (query_out) *query_out = q + 1;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static void url_decode_component(const char *src, char *dst, size_t dst_len) {
    size_t di = 0;
    for (size_t i = 0; src[i] && di + 1 < dst_len; ++i) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            int hi = hex_value(src[i+1]);
            int lo = hex_value(src[i+2]);
            if (hi >= 0 && lo >= 0) {
                dst[di++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        if (src[i] == '+') dst[di++] = ' ';
        else dst[di++] = src[i];
    }
    dst[di] = '\0';
}

static int query_get_param(const char *query, const char *key, char *out, size_t out_len) {
    if (!query || !key || !out || out_len == 0) return -1;
    size_t key_len = strlen(key);
    const char *ptr = query;
    while (*ptr) {
        const char *sep = strchr(ptr, '&');
        size_t chunk_len = sep ? (size_t)(sep - ptr) : strlen(ptr);
    const char *eq = static_cast<const char*>(std::memchr(ptr, '=', chunk_len));
        if (eq) {
            size_t klen = (size_t)(eq - ptr);
            if (klen == key_len && strncmp(ptr, key, klen) == 0) {
                size_t vlen = chunk_len - klen - 1;
                char tmp[256];
                if (vlen >= sizeof(tmp)) vlen = sizeof(tmp) - 1;
                memcpy(tmp, eq + 1, vlen);
                tmp[vlen] = '\0';
                url_decode_component(tmp, out, out_len);
                return 0;
            }
        }
        if (!sep) break;
        ptr = sep + 1;
    }
    return -1;
}

static int parse_u64(const char *s, uint64_t *out) {
    if (!s || !*s || !out) return -1;
    char *endptr = NULL;
    unsigned long long val = strtoull(s, &endptr, 10);
    if (!endptr || *endptr != '\0') return -1;
    *out = (uint64_t)val;
    return 0;
}

static int json_skip(const jsmntok_t *tokens, int count, int index) {
    int i = index + 1;
    while (i < count && tokens[i].parent >= index) {
        i++;
    }
    return i;
}

static void trim_whitespace(char *value) {
    if (!value) return;
    size_t len = strlen(value);
    size_t start = 0;
    while (start < len && isspace((unsigned char)value[start])) start++;
    size_t end = len;
    while (end > start && isspace((unsigned char)value[end - 1])) end--;
    if (start > 0 || end < len) {
        size_t new_len = end - start;
        memmove(value, value + start, new_len);
        value[new_len] = '\0';
    }
}

static int is_valid_username(const char *username) {
    if (!username) return 0;
    size_t len = strlen(username);
    if (len < 3 || len >= USERNAME_MAX) return 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)username[i];
        if (!(isalnum(c) || c == '_' || c == '.' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static int is_valid_email(const char *email) {
    if (!email) return 0;
    size_t len = strlen(email);
    if (len < 5 || len >= EMAIL_MAX) return 0;
    const char *at = strchr(email, '@');
    const char *dot = at ? strrchr(at, '.') : NULL;
    if (!at || at == email || !dot || dot <= at + 1 || dot == email + len - 1) {
        return 0;
    }
    return 1;
}

static int is_valid_password(const char *password) {
    if (!password) return 0;
    size_t len = strlen(password);
    return len >= 6 && len < PASSWORD_HASH_MAX;
}

static void handle_register(server_runtime_t *rt, http_request_t *req, http_response_t *res) {
    if (!req->body) {
        respond_with_error(res, 400, "bad_request", "Missing request body");
        return;
    }
    jsmntok_t *tokens = NULL;
    int tok_count = 0;
    if (json_parse(req->body, &tokens, &tok_count) != 0) {
        respond_with_error(res, 400, "bad_json", "Invalid JSON payload");
        return;
    }
    int user_idx = json_find_field(req->body, tokens, tok_count, "username");
    int email_idx = json_find_field(req->body, tokens, tok_count, "email");
    int pass_idx = json_find_field(req->body, tokens, tok_count, "password");
    if (user_idx < 0 || email_idx < 0 || pass_idx < 0) {
        free(tokens);
        respond_with_error(res, 400, "bad_request", "username, email, and password required");
        return;
    }
    char username[USERNAME_MAX];
    char email[EMAIL_MAX];
    char password[PASSWORD_HASH_MAX];
    if (json_copy_string(req->body, &tokens[user_idx], username, sizeof(username)) != 0 ||
        json_copy_string(req->body, &tokens[email_idx], email, sizeof(email)) != 0 ||
        json_copy_string(req->body, &tokens[pass_idx], password, sizeof(password)) != 0) {
        free(tokens);
        respond_with_error(res, 400, "bad_request", "Invalid credential fields");
        return;
    }
    trim_whitespace(username);
    trim_whitespace(email);
    trim_whitespace(password);
    if (!is_valid_username(username)) {
        free(tokens);
        respond_with_error(res, 400, "invalid_username", "Usernames must be 3-63 characters (letters, numbers, ., _, -)");
        return;
    }
    if (!is_valid_email(email)) {
        free(tokens);
        respond_with_error(res, 400, "invalid_email", "Provide a valid email address");
        return;
    }
    if (!is_valid_password(password)) {
        free(tokens);
        respond_with_error(res, 400, "invalid_password", "Passwords must be at least 6 characters");
        return;
    }

    char token[65];
    user_record_t user = {0};
    int rc = auth_service_register(rt->auth, username, email, password, token, sizeof(token), &user);
    free(tokens);
    if (rc == DB_ERR_DUP_USERNAME) {
        respond_with_error(res, 409, "username_taken", "That username is already in use");
        return;
    }
    if (rc == DB_ERR_DUP_EMAIL) {
        respond_with_error(res, 409, "email_taken", "That email address is already registered");
        return;
    }
    if (rc != 0) {
        respond_with_error(res, 500, "register_failed", "Unable to create account");
        return;
    }

    json_writer_t jw = {0};
    jw_append(&jw, "{\"token\":");
    jw_append_json_string(&jw, token);
    jw_append(&jw, ",\"user\":");
    json_write_user(&jw, &user);
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 201, "Created", &jw);
}

static void handle_login(server_runtime_t *rt, http_request_t *req, http_response_t *res) {
    if (!req->body) {
        respond_with_error(res, 400, "bad_request", "Missing request body");
        return;
    }
    jsmntok_t *tokens = NULL;
    int tok_count = 0;
    if (json_parse(req->body, &tokens, &tok_count) != 0) {
        respond_with_error(res, 400, "bad_json", "Invalid JSON payload");
        return;
    }
    int user_idx = json_find_field(req->body, tokens, tok_count, "username");
    int pass_idx = json_find_field(req->body, tokens, tok_count, "password");
    if (user_idx < 0 || pass_idx < 0) {
        free(tokens);
        respond_with_error(res, 400, "bad_request", "username and password required");
        return;
    }
    char username[USERNAME_MAX];
    char password[PASSWORD_HASH_MAX];
    if (json_copy_string(req->body, &tokens[user_idx], username, sizeof(username)) != 0 ||
        json_copy_string(req->body, &tokens[pass_idx], password, sizeof(password)) != 0) {
        free(tokens);
        respond_with_error(res, 400, "bad_request", "Invalid credential fields");
        return;
    }

    char token[65];
    user_record_t user = {0};
    if (auth_service_login(rt->auth, username, password, token, sizeof(token), &user) != 0) {
        free(tokens);
        respond_with_error(res, 401, "invalid_credentials", "Username or password incorrect");
        return;
    }
    json_writer_t jw = {0};
    jw_append(&jw, "{\"token\":");
    jw_append_json_string(&jw, token);
    jw_append(&jw, ",\"user\":");
    json_write_user(&jw, &user);
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 200, "OK", &jw);
    free(tokens);
}

static void handle_logout(server_runtime_t *rt, http_request_t *req, http_response_t *res) {
    char token[128];
    if (extract_bearer_token(req, token, sizeof(token)) != 0) {
        respond_unauthorized(res);
        return;
    }
    auth_service_logout(rt->auth, token);
    json_writer_t jw = {0};
    jw_append(&jw, "{\"success\":true}");
    respond_with_json_writer(res, 200, "OK", &jw);
}

static void handle_session(server_runtime_t *rt, http_request_t *req, http_response_t *res) {
    user_record_t user = {0};
    char token[128];
    if (ensure_authenticated(rt, req, res, &user, token, sizeof(token)) != 0) {
        return;
    }
    json_writer_t jw = {0};
    jw_append(&jw, "{\"user\":");
    json_write_user(&jw, &user);
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 200, "OK", &jw);
}

static void handle_mailboxes(server_runtime_t *rt, http_request_t *req, http_response_t *res) {
    user_record_t user = {0};
    char token[128];
    if (ensure_authenticated(rt, req, res, &user, token, sizeof(token)) != 0) {
        return;
    }
    folder_list_t folders = {0};
    if (mail_service_list_mailboxes(rt->mail, user.id, &folders) != 0) {
        respond_with_error(res, 500, "db_error", "Failed to load mailboxes");
        return;
    }
    json_writer_t jw = {0};
    jw_append(&jw, "{\"folders\":");
    json_write_folder_list(&jw, &folders);
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 200, "OK", &jw);
    folder_list_free(&folders);
}

static void handle_messages_list(server_runtime_t *rt, http_request_t *req, http_response_t *res, const char *query) {
    user_record_t user = {0};
    char token[128];
    if (ensure_authenticated(rt, req, res, &user, token, sizeof(token)) != 0) {
        return;
    }
    char folder_param[32];
    folder_kind_t folder = FOLDER_INBOX;
    if (query_get_param(query, "folder", folder_param, sizeof(folder_param)) == 0) {
        if (folder_kind_from_string(folder_param, &folder) != 0) {
            respond_with_error(res, 400, "bad_request", "Unknown folder");
            return;
        }
    }
    char custom[GROUP_NAME_MAX] = {0};
    if (query_get_param(query, "custom", custom, sizeof(custom)) != 0) {
        custom[0] = '\0';
    }
    if (folder == FOLDER_CUSTOM && custom[0] == '\0') {
        respond_with_error(res, 400, "bad_request", "custom folder name required");
        return;
    }
    message_list_t list = {0};
    if (mail_service_list_messages(rt->mail, user.id, folder, custom[0] ? custom : NULL, &list) != 0) {
        respond_with_error(res, 500, "db_error", "Failed to load messages");
        return;
    }
    json_writer_t jw = {0};
    jw_append(&jw, "{\"messages\":");
    json_write_message_list(&jw, &list);
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 200, "OK", &jw);
    message_list_free(&list);
}

static void handle_message_get(server_runtime_t *rt, http_request_t *req, http_response_t *res, uint64_t message_id) {
    user_record_t user = {0};
    char token[128];
    if (ensure_authenticated(rt, req, res, &user, token, sizeof(token)) != 0) {
        return;
    }
    message_record_t msg = {0};
    attachment_list_t attachments = {0};
    if (mail_service_get_message(rt->mail, user.id, message_id, &msg, &attachments) != 0) {
        respond_with_error(res, 404, "not_found", "Message not found");
        return;
    }
    json_writer_t jw = {0};
    jw_append(&jw, "{\"message\":");
    json_write_message(&jw, &msg);
    jw_append(&jw, ",\"attachments\":");
    json_write_attachment_list(&jw, &attachments);
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 200, "OK", &jw);
    attachment_list_free(&attachments);
}

static int copy_string_checked(const char *json, const jsmntok_t *tok, char *out, size_t out_sz) {
    if (!json || !tok || !out) return -1;
    int len = tok->end - tok->start;
    if (len < 0 || (size_t)len >= out_sz) return -1;
    memcpy(out, json + tok->start, (size_t)len);
    out[len] = '\0';
    return 0;
}

static void handle_message_compose(server_runtime_t *rt, http_request_t *req, http_response_t *res) {
    user_record_t user = {0};
    char token[128];
    if (ensure_authenticated(rt, req, res, &user, token, sizeof(token)) != 0) {
        return;
    }
    if (!req->body) {
        respond_with_error(res, 400, "bad_request", "Missing request body");
        return;
    }
    jsmntok_t *tokens = NULL;
    int tok_count = 0;
    if (json_parse(req->body, &tokens, &tok_count) != 0) {
        respond_with_error(res, 400, "bad_json", "Invalid JSON");
        return;
    }

    char subject[SUBJECT_MAX] = "";
    char body[BODY_MAX] = "";
    char recipients[RECIPIENT_MAX] = "";
    char custom[GROUP_NAME_MAX] = "";
    char archive_group[GROUP_NAME_MAX] = "";
    int save_draft = 0;
    int is_starred = 0;
    int is_archived = 0;
    compose_request_t compose = {0};
    uint64_t draft_id = 0;
    json_writer_t jw = {0};

    int subject_idx = json_find_field(req->body, tokens, tok_count, "subject");
    if (subject_idx >= 0 && tokens[subject_idx].type == JSMN_STRING) {
        json_copy_string(req->body, &tokens[subject_idx], subject, sizeof(subject));
    }
    int body_idx = json_find_field(req->body, tokens, tok_count, "body");
    if (body_idx >= 0 && tokens[body_idx].type == JSMN_STRING) {
        json_copy_string(req->body, &tokens[body_idx], body, sizeof(body));
    }
    int rcpt_idx = json_find_field(req->body, tokens, tok_count, "recipients");
    if (rcpt_idx >= 0 && tokens[rcpt_idx].type == JSMN_STRING) {
        json_copy_string(req->body, &tokens[rcpt_idx], recipients, sizeof(recipients));
    }
    int draft_idx = json_find_field(req->body, tokens, tok_count, "saveAsDraft");
    if (draft_idx >= 0) {
        json_get_bool(req->body, &tokens[draft_idx], &save_draft);
    }
    int starred_idx = json_find_field(req->body, tokens, tok_count, "starred");
    if (starred_idx >= 0) {
        json_get_bool(req->body, &tokens[starred_idx], &is_starred);
    }
    int archived_idx = json_find_field(req->body, tokens, tok_count, "archived");
    if (archived_idx >= 0) {
        json_get_bool(req->body, &tokens[archived_idx], &is_archived);
    }
    int custom_idx = json_find_field(req->body, tokens, tok_count, "customFolder");
    if (custom_idx >= 0 && tokens[custom_idx].type == JSMN_STRING) {
        json_copy_string(req->body, &tokens[custom_idx], custom, sizeof(custom));
    }
    int group_idx = json_find_field(req->body, tokens, tok_count, "archiveGroup");
    if (group_idx >= 0 && tokens[group_idx].type == JSMN_STRING) {
        json_copy_string(req->body, &tokens[group_idx], archive_group, sizeof(archive_group));
    }

    attachment_payload_t *payloads = NULL;
    size_t payload_count = 0;
    int attachments_idx = json_find_field(req->body, tokens, tok_count, "attachments");
    if (attachments_idx >= 0 && tokens[attachments_idx].type == JSMN_ARRAY) {
        payload_count = (size_t)tokens[attachments_idx].size;
        if (payload_count > 0) {
            payloads = static_cast<attachment_payload_t*>(std::calloc(payload_count, sizeof(*payloads)));
            if (!payloads) {
                free(tokens);
                respond_with_error(res, 500, "oom", "Out of memory");
                return;
            }
            int idx = attachments_idx + 1;
            for (size_t i = 0; i < payload_count; ++i) {
                if (idx >= tok_count) {
                    respond_with_error(res, 400, "bad_request", "Malformed attachments array");
                    goto compose_cleanup;
                }
                int obj_index = idx;
                if (tokens[obj_index].type != JSMN_OBJECT) {
                    respond_with_error(res, 400, "bad_request", "Attachment must be object");
                    goto compose_cleanup;
                }
                attachment_payload_t *pl = &payloads[i];
                idx = obj_index + 1;
                for (int pair = 0; pair < tokens[obj_index].size; ++pair) {
                    int key_index = idx;
                    int val_index = idx + 1;
                    if (val_index >= tok_count) {
                        respond_with_error(res, 400, "bad_request", "Malformed attachment object");
                        goto compose_cleanup;
                    }
                    if (tokens[key_index].type != JSMN_STRING) {
                        respond_with_error(res, 400, "bad_request", "Invalid attachment key");
                        goto compose_cleanup;
                    }
                    if (json_token_equals(req->body, &tokens[key_index], "filename")) {
                        if (copy_string_checked(req->body, &tokens[val_index], pl->filename, sizeof(pl->filename)) != 0) {
                            respond_with_error(res, 400, "bad_request", "Filename too long");
                            goto compose_cleanup;
                        }
                    } else if (json_token_equals(req->body, &tokens[key_index], "mimeType")) {
                        if (copy_string_checked(req->body, &tokens[val_index], pl->mime_type, sizeof(pl->mime_type)) != 0) {
                            respond_with_error(res, 400, "bad_request", "Mime type too long");
                            goto compose_cleanup;
                        }
                    } else if (json_token_equals(req->body, &tokens[key_index], "relativePath")) {
                        if (copy_string_checked(req->body, &tokens[val_index], pl->relative_path, sizeof(pl->relative_path)) != 0) {
                            respond_with_error(res, 400, "bad_request", "Attachment path too long");
                            goto compose_cleanup;
                        }
                    } else if (json_token_equals(req->body, &tokens[key_index], "data")) {
                        if (tokens[val_index].type != JSMN_STRING) {
                            respond_with_error(res, 400, "bad_request", "Attachment data must be string");
                            goto compose_cleanup;
                        }
                        int len = tokens[val_index].end - tokens[val_index].start;
                        if (len < 0) {
                            respond_with_error(res, 400, "bad_request", "Invalid attachment data length");
                            goto compose_cleanup;
                        }
                        char *data = malloc((size_t)len + 1);
                        if (!data) {
                            respond_with_error(res, 500, "oom", "Out of memory");
                            goto compose_cleanup;
                        }
                        memcpy(data, req->body + tokens[val_index].start, (size_t)len);
                        data[len] = '\0';
                        if (pl->base64_data) free(pl->base64_data);
                        pl->base64_data = data;
                    }
                    idx = json_skip(tokens, tok_count, val_index);
                }
                idx = json_skip(tokens, tok_count, obj_index);
            }
        }
    }

    compose.subject = subject;
    compose.body = body;
    compose.recipients = recipients;
    compose.save_as_draft = save_draft;
    compose.is_starred = is_starred;
    compose.is_archived = is_archived;
    compose.custom_folder = custom[0] ? custom : NULL;
    compose.archive_group = archive_group[0] ? archive_group : NULL;
    compose.attachments = payloads;
    compose.attachment_count = payload_count;
    if (mail_service_compose(rt->mail, user.id, &compose, &draft_id) != 0) {
        respond_with_error(res, 500, "compose_failed", "Failed to save message");
        goto compose_cleanup;
    }
    jw_append(&jw, "{\"success\":true");
    if (save_draft && draft_id) {
        jw_append(&jw, ",\"draftId\":");
        jw_appendf(&jw, "%llu", (unsigned long long)draft_id);
    }
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 200, "OK", &jw);

compose_cleanup:
    if (payloads) {
        for (size_t i = 0; i < payload_count; ++i) {
            free(payloads[i].base64_data);
        }
        free(payloads);
    }
    free(tokens);
}

static void handle_message_star(server_runtime_t *rt, http_request_t *req, http_response_t *res, uint64_t message_id) {
    user_record_t user = {0};
    char token[128];
    if (ensure_authenticated(rt, req, res, &user, token, sizeof(token)) != 0) {
        return;
    }
    if (!req->body) {
        respond_with_error(res, 400, "bad_request", "Missing request body");
        return;
    }
    jsmntok_t *tokens = NULL;
    int tok_count = 0;
    if (json_parse(req->body, &tokens, &tok_count) != 0) {
        respond_with_error(res, 400, "bad_json", "Invalid JSON");
        return;
    }
    int starred_idx = json_find_field(req->body, tokens, tok_count, "starred");
    int starred = 0;
    if (starred_idx < 0 || json_get_bool(req->body, &tokens[starred_idx], &starred) != 0) {
        free(tokens);
        respond_with_error(res, 400, "bad_request", "Missing starred flag");
        return;
    }
    if (mail_service_star(rt->mail, user.id, message_id, starred) != 0) {
        free(tokens);
        respond_with_error(res, 404, "not_found", "Message not found");
        return;
    }
    json_writer_t jw = {0};
    jw_append(&jw, "{\"success\":true,\"starred\":");
    jw_append(&jw, starred ? "true" : "false");
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 200, "OK", &jw);
    free(tokens);
}

static void handle_message_archive(server_runtime_t *rt, http_request_t *req, http_response_t *res, uint64_t message_id) {
    user_record_t user = {0};
    char token[128];
    if (ensure_authenticated(rt, req, res, &user, token, sizeof(token)) != 0) {
        return;
    }
    if (!req->body) {
        respond_with_error(res, 400, "bad_request", "Missing request body");
        return;
    }
    jsmntok_t *tokens = NULL;
    int tok_count = 0;
    if (json_parse(req->body, &tokens, &tok_count) != 0) {
        respond_with_error(res, 400, "bad_json", "Invalid JSON");
        return;
    }
    int archived_idx = json_find_field(req->body, tokens, tok_count, "archived");
    int archived = 0;
    if (archived_idx < 0 || json_get_bool(req->body, &tokens[archived_idx], &archived) != 0) {
        free(tokens);
        respond_with_error(res, 400, "bad_request", "Missing archived flag");
        return;
    }
    char group_name[GROUP_NAME_MAX] = "";
    int group_idx = json_find_field(req->body, tokens, tok_count, "archiveGroup");
    if (group_idx >= 0 && tokens[group_idx].type == JSMN_STRING) {
        json_copy_string(req->body, &tokens[group_idx], group_name, sizeof(group_name));
    }
    const char *group_ptr = archived ? (group_name[0] ? group_name : "") : "";
    if (mail_service_archive(rt->mail, user.id, message_id, archived, group_ptr) != 0) {
        free(tokens);
        respond_with_error(res, 404, "not_found", "Message not found");
        return;
    }
    json_writer_t jw = {0};
    jw_append(&jw, "{\"success\":true,\"archived\":");
    jw_append(&jw, archived ? "true" : "false");
    jw_append(&jw, ",\"archiveGroup\":");
    jw_append_json_string(&jw, archived ? (group_ptr ? group_ptr : "") : "");
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 200, "OK", &jw);
    free(tokens);
}

static void handle_create_folder(server_runtime_t *rt, http_request_t *req, http_response_t *res) {
    user_record_t user = {0};
    char token[128];
    if (ensure_authenticated(rt, req, res, &user, token, sizeof(token)) != 0) {
        return;
    }
    if (!req->body) {
        respond_with_error(res, 400, "bad_request", "Missing request body");
        return;
    }
    jsmntok_t *tokens = NULL;
    int tok_count = 0;
    if (json_parse(req->body, &tokens, &tok_count) != 0) {
        respond_with_error(res, 400, "bad_json", "Invalid JSON");
        return;
    }
    int name_idx = json_find_field(req->body, tokens, tok_count, "name");
    if (name_idx < 0 || tokens[name_idx].type != JSMN_STRING) {
        free(tokens);
        respond_with_error(res, 400, "bad_request", "Folder name required");
        return;
    }
    char name[GROUP_NAME_MAX];
    json_copy_string(req->body, &tokens[name_idx], name, sizeof(name));

    folder_kind_t kind = FOLDER_CUSTOM;
    int kind_idx = json_find_field(req->body, tokens, tok_count, "kind");
    if (kind_idx >= 0 && tokens[kind_idx].type == JSMN_STRING) {
        char kind_buf[32];
        json_copy_string(req->body, &tokens[kind_idx], kind_buf, sizeof(kind_buf));
        if (folder_kind_from_string(kind_buf, &kind) != 0) {
            free(tokens);
            respond_with_error(res, 400, "bad_request", "Unknown folder kind");
            return;
        }
    }

    folder_record_t folder = {0};
    if (mail_service_create_folder(rt->mail, user.id, name, kind, &folder) != 0) {
        free(tokens);
        respond_with_error(res, 500, "db_error", "Failed to create folder");
        return;
    }
    json_writer_t jw = {0};
    jw_append(&jw, "{\"folder\":");
    json_write_folder(&jw, &folder);
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 201, "Created", &jw);
    free(tokens);
}

static void handle_contacts_list(server_runtime_t *rt, http_request_t *req, http_response_t *res) {
    user_record_t user = {0};
    char token[128];
    if (ensure_authenticated(rt, req, res, &user, token, sizeof(token)) != 0) {
        return;
    }
    contact_list_t contacts = {0};
    if (mail_service_list_contacts(rt->mail, user.id, &contacts) != 0) {
        respond_with_error(res, 500, "db_error", "Failed to load contacts");
        return;
    }
    json_writer_t jw = {0};
    jw_append(&jw, "{\"contacts\":");
    json_write_contact_list(&jw, &contacts);
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 200, "OK", &jw);
    contact_list_free(&contacts);
}

static void handle_contacts_add(server_runtime_t *rt, http_request_t *req, http_response_t *res) {
    user_record_t user = {0};
    char token[128];
    if (ensure_authenticated(rt, req, res, &user, token, sizeof(token)) != 0) {
        return;
    }
    if (!req->body) {
        respond_with_error(res, 400, "bad_request", "Missing request body");
        return;
    }
    jsmntok_t *tokens = NULL;
    int tok_count = 0;
    if (json_parse(req->body, &tokens, &tok_count) != 0) {
        respond_with_error(res, 400, "bad_json", "Invalid JSON");
        return;
    }
    char alias_buf[USERNAME_MAX] = "";
    char group_buf[GROUP_NAME_MAX] = "";
    int alias_idx = json_find_field(req->body, tokens, tok_count, "alias");
    if (alias_idx >= 0 && tokens[alias_idx].type == JSMN_STRING) {
        json_copy_string(req->body, &tokens[alias_idx], alias_buf, sizeof(alias_buf));
    }
    int group_idx = json_find_field(req->body, tokens, tok_count, "groupName");
    if (group_idx >= 0 && tokens[group_idx].type == JSMN_STRING) {
        json_copy_string(req->body, &tokens[group_idx], group_buf, sizeof(group_buf));
    }
    uint64_t contact_id = 0;
    int id_idx = json_find_field(req->body, tokens, tok_count, "contactUserId");
    if (id_idx >= 0) {
        long long tmp;
        if (json_get_long(req->body, &tokens[id_idx], &tmp) == 0 && tmp > 0) {
            contact_id = (uint64_t)tmp;
        }
    }
    if (contact_id == 0) {
        int username_idx = json_find_field(req->body, tokens, tok_count, "username");
        if (username_idx < 0 || tokens[username_idx].type != JSMN_STRING) {
            free(tokens);
            respond_with_error(res, 400, "bad_request", "username or contactUserId required");
            return;
        }
        char username[USERNAME_MAX];
        json_copy_string(req->body, &tokens[username_idx], username, sizeof(username));
        user_record_t contact_user = {0};
        if (db_get_user_by_username(rt->db, username, &contact_user) != 0) {
            free(tokens);
            respond_with_error(res, 404, "not_found", "Contact user not found");
            return;
        }
        contact_id = contact_user.id;
        if (alias_buf[0] == '\0') {
            util_strlcpy(alias_buf, sizeof(alias_buf), contact_user.username);
        }
    }
    if (alias_buf[0] == '\0') {
        strncpy(alias_buf, "Friend", sizeof(alias_buf) - 1);
    }
    contact_record_t contact = {0};
    const char *group_name = group_buf[0] ? group_buf : NULL;
    if (mail_service_add_contact(rt->mail, user.id, alias_buf, group_name, contact_id, &contact) != 0) {
        free(tokens);
        respond_with_error(res, 500, "db_error", "Failed to add contact");
        return;
    }
    json_writer_t jw = {0};
    jw_append(&jw, "{\"contact\":");
    json_write_contact(&jw, &contact);
    jw_append_char(&jw, '}');
    respond_with_json_writer(res, 201, "Created", &jw);
    free(tokens);
}

static void handle_api(server_runtime_t *rt, http_request_t *req, http_response_t *res, const char *path, const char *query) {
    if (strcmp(path, "/api/register") == 0) {
        if (req->method != HTTP_POST) {
            respond_with_error(res, 405, "method_not_allowed", "Use POST");
        } else {
            handle_register(rt, req, res);
        }
        return;
    }
    if (strcmp(path, "/api/login") == 0) {
        if (req->method != HTTP_POST) {
            respond_with_error(res, 405, "method_not_allowed", "Use POST");
        } else {
            handle_login(rt, req, res);
        }
        return;
    }
    if (strcmp(path, "/api/logout") == 0) {
        if (req->method != HTTP_POST) {
            respond_with_error(res, 405, "method_not_allowed", "Use POST");
        } else {
            handle_logout(rt, req, res);
        }
        return;
    }
    if (strcmp(path, "/api/session") == 0) {
        if (req->method != HTTP_GET) {
            respond_with_error(res, 405, "method_not_allowed", "Use GET");
        } else {
            handle_session(rt, req, res);
        }
        return;
    }
    if (strcmp(path, "/api/mailboxes") == 0) {
        if (req->method != HTTP_GET) {
            respond_with_error(res, 405, "method_not_allowed", "Use GET");
        } else {
            handle_mailboxes(rt, req, res);
        }
        return;
    }
    if (strcmp(path, "/api/messages") == 0) {
        if (req->method == HTTP_GET) {
            handle_messages_list(rt, req, res, query);
        } else if (req->method == HTTP_POST) {
            handle_message_compose(rt, req, res);
        } else {
            respond_with_error(res, 405, "method_not_allowed", "Use GET or POST");
        }
        return;
    }
    if (strncmp(path, "/api/messages/", 14) == 0) {
        const char *rest = path + 14;
        const char *slash = strchr(rest, '/');
        char id_buf[32];
        if (slash) {
            size_t id_len = (size_t)(slash - rest);
            if (id_len == 0 || id_len >= sizeof(id_buf)) {
                respond_with_error(res, 400, "bad_request", "Invalid message id");
                return;
            }
            memcpy(id_buf, rest, id_len);
            id_buf[id_len] = '\0';
        } else {
            size_t len = strlen(rest);
            if (len == 0 || len >= sizeof(id_buf)) {
                respond_with_error(res, 400, "bad_request", "Invalid message id");
                return;
            }
            strncpy(id_buf, rest, sizeof(id_buf) - 1);
            id_buf[sizeof(id_buf) - 1] = '\0';
        }
        uint64_t message_id;
        if (parse_u64(id_buf, &message_id) != 0) {
            respond_with_error(res, 400, "bad_request", "Invalid message id");
            return;
        }
        if (!slash) {
            if (req->method == HTTP_GET) {
                handle_message_get(rt, req, res, message_id);
            } else {
                respond_with_error(res, 405, "method_not_allowed", "Use GET");
            }
            return;
        }
        const char *action = slash + 1;
        if (strcmp(action, "star") == 0) {
            if (req->method == HTTP_POST) {
                handle_message_star(rt, req, res, message_id);
            } else {
                respond_with_error(res, 405, "method_not_allowed", "Use POST");
            }
            return;
        }
        if (strcmp(action, "archive") == 0) {
            if (req->method == HTTP_POST) {
                handle_message_archive(rt, req, res, message_id);
            } else {
                respond_with_error(res, 405, "method_not_allowed", "Use POST");
            }
            return;
        }
        respond_with_error(res, 404, "not_found", "Unknown message action");
        return;
    }

    if (strcmp(path, "/api/folders") == 0) {
        if (req->method != HTTP_POST) {
            respond_with_error(res, 405, "method_not_allowed", "Use POST");
        } else {
            handle_create_folder(rt, req, res);
        }
        return;
    }
    if (strcmp(path, "/api/contacts") == 0) {
        if (req->method == HTTP_GET) {
            handle_contacts_list(rt, req, res);
        } else if (req->method == HTTP_POST) {
            handle_contacts_add(rt, req, res);
        } else {
            respond_with_error(res, 405, "method_not_allowed", "Use GET or POST");
        }
        return;
    }

    respond_with_error(res, 404, "not_found", "Unknown API endpoint");
}

void router_init(server_runtime_t *rt) {
    (void)rt;
}

void router_dispose(void) {}

int router_handle_request(server_runtime_t *rt, http_request_t *req, router_result_t *out) {
    http_response_init(&out->response);
    const char *conn = http_header_get(req, "Connection");
    if (conn && strcasecmp(conn, "close") == 0) {
        out->response.keep_alive = 0;
    }

    if (req->method == HTTP_OPTIONS) {
        set_common_headers(&out->response);
        out->response.status_code = 204;
        strncpy(out->response.status_text, "No Content", sizeof(out->response.status_text) - 1);
        out->response.status_text[sizeof(out->response.status_text) - 1] = '\0';
        return 0;
    }

    char path[sizeof(req->path)];
    const char *query = NULL;
    split_path_query(req->path, path, sizeof(path), &query);

    if (req->method == HTTP_GET && strncmp(path, "/static/", 8) == 0) {
        respond_with_static(rt, &out->response, path + 8);
        return 0;
    }
    if (req->method == HTTP_GET && (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        respond_with_static(rt, &out->response, "learn.html");
        return 0;
    }
    if (req->method == HTTP_GET && strcmp(path, "/learn.html") == 0) {
        respond_with_static(rt, &out->response, "learn.html");
        return 0;
    }
    if (req->method == HTTP_GET && (strcmp(path, "/mail") == 0 || strcmp(path, "/mail/") == 0)) {
        template_var_t vars[] = {
            {"title", "MailCenter 登录"}
        };
        respond_with_template(rt, &out->response, "login.html", vars, sizeof(vars)/sizeof(vars[0]));
        return 0;
    }
    if (req->method == HTTP_GET && (strcmp(path, "/mail/app") == 0 || strcmp(path, "/mail/app/") == 0 || strcmp(path, "/app") == 0)) {
        template_var_t vars[] = {
            {"title", "收件箱"}
        };
        respond_with_template(rt, &out->response, "app.html", vars, sizeof(vars)/sizeof(vars[0]));
        return 0;
    }
    if (strncmp(path, "/api/", 5) == 0) {
        handle_api(rt, req, &out->response, path, query);
        return 0;
    }

    if (req->method == HTTP_GET && strncmp(path, "/mail", 5) != 0 && strncmp(path, "/api/", 5) != 0 && strncmp(path, "/static/", 8) != 0) {
        const char *rel = path;
        while (*rel == '/') rel++;
        if (*rel == '\0') {
            respond_with_static(rt, &out->response, "learn.html");
        } else {
            respond_with_static(rt, &out->response, rel);
        }
        return 0;
    }

    respond_with_error(&out->response, 404, "not_found", "Resource not found");
    return 0;
}

