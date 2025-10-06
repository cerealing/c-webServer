#include "template_engine.h"

#include "logger.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

struct template_engine {
    char root[512];
};

static int read_whole_file(const char *path, char **out_buf, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOGE("template: failed to open %s", path);
        return -1;
    }
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
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
    std::free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buf[sz] = '\0';
    if (out_len) *out_len = (size_t)sz;
    *out_buf = buf;
    return 0;
}

template_engine_t *template_engine_create(const char *template_dir) {
    template_engine_t *engine = static_cast<template_engine_t*>(std::calloc(1, sizeof(*engine)));
    if (!engine) return NULL;
    if (!template_dir || !*template_dir) {
        strcpy(engine->root, "templates");
    } else {
        strncpy(engine->root, template_dir, sizeof(engine->root) - 1);
    }
    return engine;
}

void template_engine_destroy(template_engine_t *engine) {
    if (!engine) return;
    std::free(engine);
}

static const char *find_var(const template_var_t *vars, size_t count, const char *key, size_t key_len) {
    for (size_t i = 0; i < count; ++i) {
        if (!vars[i].key) continue;
        size_t len = strlen(vars[i].key);
        if (len == key_len && strncmp(vars[i].key, key, key_len) == 0) {
            return vars[i].value ? vars[i].value : "";
        }
    }
    return "";
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} str_builder_t;

static int sb_ensure(str_builder_t *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return 0;
    size_t new_cap = sb->cap == 0 ? 1024 : sb->cap;
    while (sb->len + extra + 1 > new_cap) new_cap *= 2;
    char *tmp = static_cast<char*>(std::realloc(sb->buf, new_cap));
    if (!tmp) return -1;
    sb->buf = tmp;
    sb->cap = new_cap;
    return 0;
}

static int sb_append_mem(str_builder_t *sb, const char *data, size_t len) {
    if (sb_ensure(sb, len) != 0) return -1;
    memcpy(sb->buf + sb->len, data, len);
    sb->len += len;
    sb->buf[sb->len] = '\0';
    return 0;
}

int template_engine_render(template_engine_t *engine, const char *name,
                           const template_var_t *vars, size_t var_count,
                           char **out_html, size_t *out_len) {
    if (!engine || !name || !out_html) return -1;
    char path[768];
    snprintf(path, sizeof(path), "%s/%s", engine->root, name);

    char *raw = NULL;
    size_t raw_len = 0;
    if (read_whole_file(path, &raw, &raw_len) != 0) {
        return -1;
    }

    str_builder_t sb = {0};
    sb_ensure(&sb, raw_len + 1);

    for (size_t i = 0; i < raw_len;) {
        if (i + 1 < raw_len && raw[i] == '{' && raw[i+1] == '{') {
            size_t start = i + 2;
            size_t end = start;
            while (end < raw_len && !(raw[end] == '}' && end + 1 < raw_len && raw[end+1] == '}')) {
                end++;
            }
            if (end + 1 >= raw_len) {
                sb_append_mem(&sb, raw + i, raw_len - i);
                break;
            }
            size_t key_start = start;
            while (key_start < end && (raw[key_start] == ' ' || raw[key_start] == '\t' || raw[key_start] == '\n')) key_start++;
            size_t key_end = end - 1;
            while (key_end > key_start && (raw[key_end] == ' ' || raw[key_end] == '\t' || raw[key_end] == '\n')) key_end--;
            if (key_end < key_start) {
                // empty key, skip
            } else {
                size_t key_len = key_end - key_start + 1;
                const char *val = find_var(vars, var_count, raw + key_start, key_len);
                sb_append_mem(&sb, val, strlen(val));
            }
            i = end + 2;
        } else {
            sb_append_mem(&sb, &raw[i], 1);
            i++;
        }
    }

    std::free(raw);
    if (!sb.buf) {
    sb.buf = static_cast<char*>(std::calloc(1, 1));
        sb.cap = 1;
    }
    *out_html = sb.buf;
    if (out_len) *out_len = sb.len;
    return 0;
}
