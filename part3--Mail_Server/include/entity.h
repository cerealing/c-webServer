#ifndef ENTITY_H
#define ENTITY_H

#include <stdint.h>
#include <time.h>

#define USERNAME_MAX 64
#define EMAIL_MAX 128
#define PASSWORD_HASH_MAX 128
#define SUBJECT_MAX 256
#define BODY_MAX 16384
#define ATTACHMENT_NAME_MAX 128
#define ATTACHMENT_PATH_MAX 256
#define ATTACHMENT_REL_PATH_MAX 256
#define RECIPIENT_MAX 128
#define GROUP_NAME_MAX 64

typedef enum {
    FOLDER_INBOX,
    FOLDER_SENT,
    FOLDER_DRAFTS,
    FOLDER_STARRED,
    FOLDER_ARCHIVE,
    FOLDER_CUSTOM
} folder_kind_t;

typedef struct {
    uint64_t id;
    char username[USERNAME_MAX];
    char email[EMAIL_MAX];
    char password_hash[PASSWORD_HASH_MAX];
    time_t created_at;
} user_record_t;

typedef struct {
    uint64_t id;
    uint64_t owner_id;
    folder_kind_t kind;
    char name[GROUP_NAME_MAX];
    time_t created_at;
} folder_record_t;

typedef struct {
    uint64_t id;
    uint64_t message_id;
    char filename[ATTACHMENT_NAME_MAX];
    char storage_path[ATTACHMENT_PATH_MAX];
    char relative_path[ATTACHMENT_REL_PATH_MAX];
    char mime_type[64];
    uint64_t size_bytes;
} attachment_record_t;

typedef struct {
    uint64_t id;
    uint64_t owner_id;
    folder_kind_t folder;
    char custom_folder[GROUP_NAME_MAX];
    char archive_group[GROUP_NAME_MAX];
    char subject[SUBJECT_MAX];
    char body[BODY_MAX];
    char recipients[RECIPIENT_MAX];
    int is_starred;
    int is_draft;
    int is_archived;
    time_t created_at;
    time_t updated_at;
} message_record_t;

typedef struct {
    uint64_t id;
    uint64_t user_id;
    uint64_t contact_user_id;
    char alias[USERNAME_MAX];
    char group_name[GROUP_NAME_MAX];
    time_t created_at;
} contact_record_t;

#endif // ENTITY_H
