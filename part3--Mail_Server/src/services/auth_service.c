#include "services/auth_service.h"
#include "util.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#define SESSION_EXPIRY_SECS (12 * 60 * 60)

typedef struct session_node {
    session_record_t session;
} session_node_t;

struct auth_context {
    db_handle_t *db;
    session_node_t *sessions;
    size_t count;
    size_t capacity;
    pthread_mutex_t mutex;
};

static void ensure_session_capacity(auth_context_t *ctx) {
    if (ctx->count < ctx->capacity) return;
    size_t new_cap = ctx->capacity == 0 ? 16 : ctx->capacity * 2;
    session_node_t *tmp = realloc(ctx->sessions, new_cap * sizeof(session_node_t));
    if (!tmp) return;
    ctx->sessions = tmp;
    ctx->capacity = new_cap;
}

auth_context_t *auth_service_create(db_handle_t *db) {
    auth_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->db = db;
    pthread_mutex_init(&ctx->mutex, NULL);
    return ctx;
}

void auth_service_destroy(auth_context_t *ctx) {
    if (!ctx) return;
    pthread_mutex_destroy(&ctx->mutex);
    free(ctx->sessions);
    free(ctx);
}

static void token_to_hex(uint64_t hi, uint64_t lo, char *out) {
    snprintf(out, 65, "%016llx%016llx%016llx%016llx",
             (unsigned long long)(hi >> 32),
             (unsigned long long)(hi & 0xffffffffULL),
             (unsigned long long)(lo >> 32),
             (unsigned long long)(lo & 0xffffffffULL));
}

static session_record_t *find_session(auth_context_t *ctx, const char *token) {
    for (size_t i = 0; i < ctx->count; ++i) {
        if (strcmp(ctx->sessions[i].session.token, token) == 0) {
            return &ctx->sessions[i].session;
        }
    }
    return NULL;
}

static void prune_expired(auth_context_t *ctx) {
    time_t now = time(NULL);
    size_t w = 0;
    for (size_t i = 0; i < ctx->count; ++i) {
        if (ctx->sessions[i].session.expires_at > now) {
            ctx->sessions[w++] = ctx->sessions[i];
        }
    }
    ctx->count = w;
}

int auth_service_login(auth_context_t *ctx, const char *username, const char *password,
                       char *token_out, size_t token_len, user_record_t *user_out) {
    if (!ctx || !username || !password || !token_out) return -1;
    user_record_t user;
    if (db_authenticate(ctx->db, username, password, &user) != 0) {
        return -1;
    }

    pthread_mutex_lock(&ctx->mutex);
    prune_expired(ctx);

    uint64_t hi = util_rand64();
    uint64_t lo = util_rand64();
    char token[65];
    token_to_hex(hi, lo, token);
    if (token_len < sizeof(token)) {
        pthread_mutex_unlock(&ctx->mutex);
        return -1;
    }
    ensure_session_capacity(ctx);
    if (ctx->count == ctx->capacity) {
        pthread_mutex_unlock(&ctx->mutex);
        return -1;
    }
    session_record_t rec = {
        .user_id = user.id,
        .expires_at = time(NULL) + SESSION_EXPIRY_SECS
    };
    strcpy(rec.token, token);
    ctx->sessions[ctx->count++].session = rec;
    pthread_mutex_unlock(&ctx->mutex);

    strncpy(token_out, token, token_len);
    if (user_out) *user_out = user;
    return 0;
}

int auth_service_logout(auth_context_t *ctx, const char *token) {
    if (!ctx || !token) return -1;
    pthread_mutex_lock(&ctx->mutex);
    prune_expired(ctx);
    size_t w = 0;
    for (size_t i = 0; i < ctx->count; ++i) {
        if (strcmp(ctx->sessions[i].session.token, token) != 0) {
            ctx->sessions[w++] = ctx->sessions[i];
        }
    }
    ctx->count = w;
    pthread_mutex_unlock(&ctx->mutex);
    return 0;
}

int auth_service_validate(auth_context_t *ctx, const char *token, user_record_t *user_out) {
    if (!ctx || !token) return -1;
    pthread_mutex_lock(&ctx->mutex);
    prune_expired(ctx);
    session_record_t *session = find_session(ctx, token);
    if (!session) {
        pthread_mutex_unlock(&ctx->mutex);
        return -1;
    }
    session->expires_at = time(NULL) + SESSION_EXPIRY_SECS;
    uint64_t user_id = session->user_id;
    pthread_mutex_unlock(&ctx->mutex);

    if (user_out) {
        if (db_get_user_by_id(ctx->db, user_id, user_out) != 0) {
            return -1;
        }
    }
    return 0;
}

int auth_service_register(auth_context_t *ctx, const char *username, const char *email, const char *password,
                          char *token_out, size_t token_len, user_record_t *user_out) {
    if (!ctx || !username || !email || !password || !token_out) return -1;
    user_record_t created = {0};
    int rc = db_create_user(ctx->db, username, email, password, &created);
    if (rc != 0) {
        return rc;
    }
    return auth_service_login(ctx, username, password, token_out, token_len, user_out);
}
