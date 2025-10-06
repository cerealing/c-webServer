#ifndef MAIL_SERVICE_H
#define MAIL_SERVICE_H

#include "db.h"

typedef struct mail_service mail_service_t;

typedef struct {
    char filename[ATTACHMENT_NAME_MAX];
    char mime_type[64];
    char relative_path[ATTACHMENT_REL_PATH_MAX];
    char *base64_data;
} attachment_payload_t;

typedef struct {
    const char *subject;
    const char *body;
    const char *recipients;
    int save_as_draft;
    int is_starred;
    int is_archived;
    const char *custom_folder;
    const char *archive_group;
    attachment_payload_t *attachments;
    size_t attachment_count;
} compose_request_t;

mail_service_t *mail_service_create(db_handle_t *db, const mail::ServerConfig &cfg);
void mail_service_destroy(mail_service_t *svc);

int mail_service_list_mailboxes(mail_service_t *svc, uint64_t user_id, folder_list_t *out);
int mail_service_list_messages(mail_service_t *svc, uint64_t user_id, folder_kind_t folder, const char *custom, message_list_t *out);
int mail_service_get_message(mail_service_t *svc, uint64_t user_id, uint64_t message_id, message_record_t *msg, attachment_list_t *attachments);
int mail_service_compose(mail_service_t *svc, uint64_t user_id, const compose_request_t *compose, uint64_t *draft_id_out);
int mail_service_star(mail_service_t *svc, uint64_t user_id, uint64_t message_id, int starred);
int mail_service_archive(mail_service_t *svc, uint64_t user_id, uint64_t message_id, int archived, const char *group_name);
int mail_service_create_folder(mail_service_t *svc, uint64_t user_id, const char *name, folder_kind_t kind, folder_record_t *out_folder);
int mail_service_list_contacts(mail_service_t *svc, uint64_t user_id, contact_list_t *out);
int mail_service_add_contact(mail_service_t *svc, uint64_t user_id, const char *alias, const char *group_name, uint64_t contact_user_id, contact_record_t *out);

#endif // MAIL_SERVICE_H
