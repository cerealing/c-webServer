#include "services/mail_service.h"
#include "logger.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

struct mail_service {
    db_handle_t *db;
    char upload_root[512];
};

static int ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        return -1;
    }
    return mkdir(path, 0755);
}

static unsigned char base64_decode_table[256];
static int base64_table_ready = 0;

static void base64_init(void) {
    if (base64_table_ready) return;
    memset(base64_decode_table, 0x80, sizeof(base64_decode_table));
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) {
        base64_decode_table[(unsigned char)alphabet[i]] = i;
    }
    base64_decode_table[(unsigned char)'='] = 0;
    base64_table_ready = 1;
}

static int base64_decode(const char *input, unsigned char **out_buf, size_t *out_len) {
    base64_init();
    size_t len = strlen(input);
    if (len % 4 != 0) return -1;
    size_t pad = 0;
    if (len >= 2) {
        if (input[len-1] == '=') pad++;
        if (input[len-2] == '=') pad++;
    }
    size_t decoded_len = (len / 4) * 3 - pad;
    unsigned char *buf = malloc(decoded_len + 1);
    if (!buf) return -1;

    size_t o = 0;
    for (size_t i = 0; i < len; i += 4) {
        unsigned char c0 = base64_decode_table[(unsigned char)input[i]];
        unsigned char c1 = base64_decode_table[(unsigned char)input[i+1]];
        unsigned char c2 = base64_decode_table[(unsigned char)input[i+2]];
        unsigned char c3 = base64_decode_table[(unsigned char)input[i+3]];
        if (c0 & 0x80 || c1 & 0x80 || c2 & 0x80 || c3 & 0x80) {
            free(buf);
            return -1;
        }
        buf[o++] = (unsigned char)((c0 << 2) | (c1 >> 4));
        if (input[i+2] != '=') {
            buf[o++] = (unsigned char)((c1 << 4) | (c2 >> 2));
        }
        if (input[i+3] != '=') {
            buf[o++] = (unsigned char)((c2 << 6) | c3);
        }
    }
    *out_buf = buf;
    *out_len = decoded_len;
    return 0;
}

mail_service_t *mail_service_create(db_handle_t *db, const server_config *cfg) {
    mail_service_t *svc = calloc(1, sizeof(*svc));
    if (!svc) return NULL;
    svc->db = db;
    snprintf(svc->upload_root, sizeof(svc->upload_root), "%s/uploads", cfg->data_dir[0] ? cfg->data_dir : "data");
    ensure_directory(cfg->data_dir[0] ? cfg->data_dir : "data");
    ensure_directory(svc->upload_root);
    return svc;
}

void mail_service_destroy(mail_service_t *svc) {
    if (!svc) return;
    free(svc);
}

int mail_service_list_mailboxes(mail_service_t *svc, uint64_t user_id, folder_list_t *out) {
    return db_list_folders(svc->db, user_id, out);
}

int mail_service_list_messages(mail_service_t *svc, uint64_t user_id, folder_kind_t folder, const char *custom, message_list_t *out) {
    return db_list_messages(svc->db, user_id, folder, custom, out);
}

int mail_service_get_message(mail_service_t *svc, uint64_t user_id, uint64_t message_id, message_record_t *msg, attachment_list_t *attachments) {
    return db_get_message(svc->db, user_id, message_id, msg, attachments);
}

static int ensure_user_upload_dir(mail_service_t *svc, uint64_t user_id, char *out_path, size_t out_len) {
    int written = snprintf(out_path, out_len, "%s/%llu", svc->upload_root, (unsigned long long)user_id);
    if (written < 0 || (size_t)written >= out_len) {
        LOGE("Upload path too long for user %llu", (unsigned long long)user_id);
        return -1;
    }
    if (ensure_directory(out_path) != 0) {
        LOGE("Failed to ensure upload directory %s", out_path);
        return -1;
    }
    return 0;
}

static int store_attachment(mail_service_t *svc, uint64_t user_id, const attachment_payload_t *payload,
                            attachment_record_t *out_rec) {
    const char *encoded = payload->base64_data ? payload->base64_data : "";
    if (!encoded[0]) {
        memset(out_rec, 0, sizeof(*out_rec));
        util_strlcpy(out_rec->filename, sizeof(out_rec->filename), payload->filename);
        util_strlcpy(out_rec->mime_type, sizeof(out_rec->mime_type), payload->mime_type);
        util_strlcpy(out_rec->relative_path, sizeof(out_rec->relative_path), payload->relative_path);
        if (out_rec->relative_path[0] == '\0') {
            util_strlcpy(out_rec->relative_path, sizeof(out_rec->relative_path), payload->filename);
        }
        return 0;
    }
    unsigned char *binary = NULL;
    size_t binary_len = 0;
    if (base64_decode(encoded, &binary, &binary_len) != 0) {
        return -1;
    }
    char user_dir[512];
    if (ensure_user_upload_dir(svc, user_id, user_dir, sizeof(user_dir)) != 0) {
        free(binary);
        return -1;
    }
    char filename[ATTACHMENT_NAME_MAX];
    int n = snprintf(filename, sizeof(filename), "%llu-%llu-%s",
                     (unsigned long long)util_now_ms(),
                     (unsigned long long)util_rand64(),
                     payload->filename);
    if (n < 0 || (size_t)n >= sizeof(filename)) {
        free(binary);
        return -1;
    }
    char fullpath[ATTACHMENT_PATH_MAX];
    n = snprintf(fullpath, sizeof(fullpath), "%s/%s", user_dir, filename);
    if (n < 0 || (size_t)n >= sizeof(fullpath)) {
        free(binary);
        return -1;
    }
    FILE *fp = fopen(fullpath, "wb");
    if (!fp) {
        free(binary);
        return -1;
    }
    if (fwrite(binary, 1, binary_len, fp) != binary_len) {
        fclose(fp);
        free(binary);
        return -1;
    }
    fclose(fp);
    free(binary);

    memset(out_rec, 0, sizeof(*out_rec));
    util_strlcpy(out_rec->filename, sizeof(out_rec->filename), payload->filename);
    util_strlcpy(out_rec->storage_path, sizeof(out_rec->storage_path), fullpath);
    util_strlcpy(out_rec->mime_type, sizeof(out_rec->mime_type), payload->mime_type);
    util_strlcpy(out_rec->relative_path, sizeof(out_rec->relative_path), payload->relative_path);
    if (out_rec->relative_path[0] == '\0') {
        util_strlcpy(out_rec->relative_path, sizeof(out_rec->relative_path), payload->filename);
    }
    out_rec->size_bytes = binary_len;
    return 0;
}

int mail_service_compose(mail_service_t *svc, uint64_t user_id, const compose_request_t *compose, uint64_t *draft_id_out) {
    if (!compose) return -1;
    message_record_t msg;
    memset(&msg, 0, sizeof(msg));
    util_strlcpy(msg.subject, sizeof(msg.subject), compose->subject ? compose->subject : "(no subject)");
    util_strlcpy(msg.body, sizeof(msg.body), compose->body ? compose->body : "");
    util_strlcpy(msg.recipients, sizeof(msg.recipients), compose->recipients ? compose->recipients : "");
    msg.is_starred = compose->is_starred;
    msg.is_archived = compose->is_archived;
    if (compose->archive_group) {
        util_strlcpy(msg.archive_group, sizeof(msg.archive_group), compose->archive_group);
    }
    if (compose->custom_folder) {
        msg.folder = FOLDER_CUSTOM;
        util_strlcpy(msg.custom_folder, sizeof(msg.custom_folder), compose->custom_folder);
    }

    attachment_list_t attachments = {0};
    if (compose->attachment_count > 0) {
        attachments.items = calloc(compose->attachment_count, sizeof(attachment_record_t));
        attachments.count = compose->attachment_count;
        for (size_t i = 0; i < compose->attachment_count; ++i) {
            if (store_attachment(svc, user_id, &compose->attachments[i], &attachments.items[i]) != 0) {
                attachment_list_free(&attachments);
                return -1;
            }
        }
    }

    int rc;
    if (compose->save_as_draft) {
        rc = db_save_draft(svc->db, user_id, &msg, &attachments);
        if (draft_id_out) {
            *draft_id_out = msg.id;
        }
    } else {
        rc = db_send_message(svc->db, user_id, &msg, &attachments);
        if (draft_id_out) *draft_id_out = 0;
    }

    attachment_list_free(&attachments);
    return rc;
}

int mail_service_star(mail_service_t *svc, uint64_t user_id, uint64_t message_id, int starred) {
    return db_star_message(svc->db, user_id, message_id, starred);
}

int mail_service_archive(mail_service_t *svc, uint64_t user_id, uint64_t message_id, int archived, const char *group_name) {
    return db_archive_message(svc->db, user_id, message_id, archived, group_name);
}

int mail_service_create_folder(mail_service_t *svc, uint64_t user_id, const char *name, folder_kind_t kind, folder_record_t *out_folder) {
    return db_create_folder(svc->db, user_id, name, kind, out_folder);
}

int mail_service_list_contacts(mail_service_t *svc, uint64_t user_id, contact_list_t *out) {
    return db_list_contacts(svc->db, user_id, out);
}

int mail_service_add_contact(mail_service_t *svc, uint64_t user_id, const char *alias, const char *group_name, uint64_t contact_user_id, contact_record_t *out) {
    return db_add_contact(svc->db, user_id, alias, group_name, contact_user_id, out);
}
