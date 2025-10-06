#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include "db.h"

#include <stddef.h>

typedef struct auth_context auth_context_t;

auth_context_t *auth_service_create(db_handle_t *db);
void auth_service_destroy(auth_context_t *ctx);
int auth_service_login(auth_context_t *ctx, const char *username, const char *password,
                       char *token_out, size_t token_len, user_record_t *user_out);
int auth_service_logout(auth_context_t *ctx, const char *token);
int auth_service_validate(auth_context_t *ctx, const char *token, user_record_t *user_out);
int auth_service_register(auth_context_t *ctx, const char *username, const char *email, const char *password,
                          char *token_out, size_t token_len, user_record_t *user_out);

#endif // AUTH_SERVICE_H
