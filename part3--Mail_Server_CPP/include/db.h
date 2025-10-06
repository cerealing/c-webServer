#ifndef DB_H
#define DB_H

#include "entity.h"
#include "config.h"

#include <stddef.h>
#include <stdlib.h>

typedef struct db_handle db_handle_t;

#define DB_ERR_DUP_USERNAME (-2)
#define DB_ERR_DUP_EMAIL (-3)

typedef struct {
    message_record_t *items;
    size_t count;
} message_list_t;

typedef struct {
    folder_record_t *items;
    size_t count;
} folder_list_t;

typedef struct {
    attachment_record_t *items;
    size_t count;
} attachment_list_t;

typedef struct {
    contact_record_t *items;
    size_t count;
} contact_list_t;

typedef struct {
    uint64_t user_id;
    char token[65];
    time_t expires_at;
} session_record_t;

static inline void message_list_free(message_list_t *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static inline void folder_list_free(folder_list_t *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static inline void attachment_list_free(attachment_list_t *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static inline void contact_list_free(contact_list_t *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

int db_init(const mail::ServerConfig &cfg, db_handle_t **out);
void db_close(db_handle_t *db);

int db_authenticate(db_handle_t *db, const char *username, const char *password, user_record_t *out_user);
int db_get_user_by_id(db_handle_t *db, uint64_t user_id, user_record_t *out_user);
int db_get_user_by_username(db_handle_t *db, const char *username, user_record_t *out_user);
int db_create_user(db_handle_t *db, const char *username, const char *email, const char *password, user_record_t *out_user);

int db_list_folders(db_handle_t *db, uint64_t user_id, folder_list_t *out);
int db_create_folder(db_handle_t *db, uint64_t user_id, const char *name, folder_kind_t kind, folder_record_t *out_folder);

int db_list_messages(db_handle_t *db, uint64_t user_id, folder_kind_t folder, const char *custom, message_list_t *out);
int db_get_message(db_handle_t *db, uint64_t user_id, uint64_t message_id, message_record_t *out, attachment_list_t *attachments);
int db_save_draft(db_handle_t *db, uint64_t user_id, message_record_t *msg, attachment_list_t *attachments);
int db_send_message(db_handle_t *db, uint64_t user_id, const message_record_t *msg, const attachment_list_t *attachments);
int db_star_message(db_handle_t *db, uint64_t user_id, uint64_t message_id, int starred);
int db_archive_message(db_handle_t *db, uint64_t user_id, uint64_t message_id, int archived, const char *group_name);

int db_list_contacts(db_handle_t *db, uint64_t user_id, contact_list_t *out);
int db_add_contact(db_handle_t *db, uint64_t user_id, const char *alias, const char *group_name, uint64_t contact_user_id, contact_record_t *out);

#endif // DB_H
