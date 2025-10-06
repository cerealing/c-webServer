#include "config.h"
#include "jsmn.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_string(const char *json, const jsmntok_t *tok, char *out, size_t out_sz) {
    int len = tok->end - tok->start;
    if (len >= (int)out_sz) len = (int)out_sz - 1;
    memcpy(out, json + tok->start, len);
    out[len] = '\0';
    return 0;
}

static int token_streq(const char *json, const jsmntok_t *tok, const char *s) {
    int len = tok->end - tok->start;
    return ((int)strlen(s) == len) && strncmp(json + tok->start, s, len) == 0;
}

void config_set_defaults(server_config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->listen_address, "0.0.0.0");
    cfg->port = 8085;
    cfg->max_connections = 64;
    cfg->thread_pool_size = 8;
    strcpy(cfg->static_dir, "static");
    strcpy(cfg->template_dir, "templates");
    strcpy(cfg->data_dir, "data");
    strcpy(cfg->log_path, "-");
    cfg->backend = DB_BACKEND_STUB;
    strcpy(cfg->mysql.host, "127.0.0.1");
    cfg->mysql.port = 3306;
    strcpy(cfg->mysql.user, "root");
    strcpy(cfg->mysql.password, "123456789");
    strcpy(cfg->mysql.database, "mail_app");
    cfg->mysql.pool_size = 10;
    strcpy(cfg->session_secret, "change-me");
}

int config_load(const char *path, server_config *cfg) {
    config_set_defaults(cfg);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        LOGW("config: could not open %s, using defaults", path);
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
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    char *json = malloc((size_t)sz + 1);
    if (!json) {
        fclose(fp);
        return -1;
    }
    size_t read = fread(json, 1, (size_t)sz, fp);
    if (read != (size_t)sz) {
        free(json);
        fclose(fp);
        return -1;
    }
    json[sz] = '\0';
    fclose(fp);

    jsmn_parser parser;
    jsmn_init(&parser);
    int token_count = 256;
    jsmntok_t *tokens = calloc(token_count, sizeof(jsmntok_t));
    if (!tokens) {
        free(json);
        return -1;
    }
    int r = jsmn_parse(&parser, json, (unsigned int)sz, tokens, token_count);
    if (r < 0) {
        free(tokens);
        free(json);
        return -1;
    }

    for (int i = 1; i < r; ++i) {
        jsmntok_t *t = &tokens[i];
        if (t->type != JSMN_STRING) continue;
        if (token_streq(json, t, "listen_address")) {
            parse_string(json, &tokens[++i], cfg->listen_address, sizeof(cfg->listen_address));
        } else if (token_streq(json, t, "port")) {
            cfg->port = atoi(json + tokens[++i].start);
        } else if (token_streq(json, t, "max_connections")) {
            cfg->max_connections = atoi(json + tokens[++i].start);
        } else if (token_streq(json, t, "thread_pool_size")) {
            cfg->thread_pool_size = atoi(json + tokens[++i].start);
        } else if (token_streq(json, t, "static_dir")) {
            parse_string(json, &tokens[++i], cfg->static_dir, sizeof(cfg->static_dir));
        } else if (token_streq(json, t, "template_dir")) {
            parse_string(json, &tokens[++i], cfg->template_dir, sizeof(cfg->template_dir));
        } else if (token_streq(json, t, "data_dir")) {
            parse_string(json, &tokens[++i], cfg->data_dir, sizeof(cfg->data_dir));
        } else if (token_streq(json, t, "log_path")) {
            parse_string(json, &tokens[++i], cfg->log_path, sizeof(cfg->log_path));
        } else if (token_streq(json, t, "db_backend")) {
            char val[32];
            parse_string(json, &tokens[++i], val, sizeof(val));
            if (strcmp(val, "mysql") == 0) cfg->backend = DB_BACKEND_MYSQL;
            else cfg->backend = DB_BACKEND_STUB;
        } else if (token_streq(json, t, "session_secret")) {
            parse_string(json, &tokens[++i], cfg->session_secret, sizeof(cfg->session_secret));
        } else if (token_streq(json, t, "mysql")) {
            int obj_size = tokens[i+1].size;
            int limit = i + 1 + obj_size * 2;
            for (int j = i + 1; j <= limit && j < r; j += 2) {
                if (tokens[j].type != JSMN_STRING) continue;
                if (token_streq(json, &tokens[j], "host")) {
                    parse_string(json, &tokens[j+1], cfg->mysql.host, sizeof(cfg->mysql.host));
                } else if (token_streq(json, &tokens[j], "port")) {
                    cfg->mysql.port = atoi(json + tokens[j+1].start);
                } else if (token_streq(json, &tokens[j], "user")) {
                    parse_string(json, &tokens[j+1], cfg->mysql.user, sizeof(cfg->mysql.user));
                } else if (token_streq(json, &tokens[j], "password")) {
                    parse_string(json, &tokens[j+1], cfg->mysql.password, sizeof(cfg->mysql.password));
                } else if (token_streq(json, &tokens[j], "database")) {
                    parse_string(json, &tokens[j+1], cfg->mysql.database, sizeof(cfg->mysql.database));
                } else if (token_streq(json, &tokens[j], "pool_size")) {
                    cfg->mysql.pool_size = atoi(json + tokens[j+1].start);
                }
            }
        }
    }

    free(tokens);
    free(json);
    return 0;
}
