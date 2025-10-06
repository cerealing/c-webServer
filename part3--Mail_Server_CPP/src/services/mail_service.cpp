#include "services/mail_service.h"
#include "logger.h"
#include "util.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

struct mail_service {
    db_handle_t *db;
    std::filesystem::path upload_root;
};

namespace {

bool ensure_directory(const std::filesystem::path &path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return std::filesystem::is_directory(path, ec);
    }
    std::filesystem::create_directories(path, ec);
    return !ec;
}

std::filesystem::path resolve_data_root(const mail::ServerConfig &cfg) {
    if (cfg.data_dir.empty()) {
        return std::filesystem::path{"data"};
    }
    return cfg.data_dir;
}

} // namespace

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
    unsigned char *buf = static_cast<unsigned char*>(std::malloc(decoded_len + 1));
    if (!buf) return -1;

    size_t o = 0;
    for (size_t i = 0; i < len; i += 4) {
        unsigned char c0 = base64_decode_table[(unsigned char)input[i]];
        unsigned char c1 = base64_decode_table[(unsigned char)input[i+1]];
        unsigned char c2 = base64_decode_table[(unsigned char)input[i+2]];
        unsigned char c3 = base64_decode_table[(unsigned char)input[i+3]];
        if (c0 & 0x80 || c1 & 0x80 || c2 & 0x80 || c3 & 0x80) {
                std::free(buf);
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

mail_service_t *mail_service_create(db_handle_t *db, const mail::ServerConfig &cfg) {
    if (!db) {
        return nullptr;
    }

    auto svc = std::unique_ptr<mail_service>(new mail_service{});
    svc->db = db;

    const auto data_root = resolve_data_root(cfg);
    if (!ensure_directory(data_root)) {
        LOGE("Failed to prepare data directory %s", data_root.string().c_str());
        return nullptr;
    }

    svc->upload_root = data_root / "uploads";
    if (!ensure_directory(svc->upload_root)) {
        LOGE("Failed to prepare upload directory %s", svc->upload_root.string().c_str());
        return nullptr;
    }

    return svc.release();
}

void mail_service_destroy(mail_service_t *svc) {
    if (!svc) return;
    delete svc;
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

static std::optional<std::filesystem::path> ensure_user_upload_dir(mail_service_t *svc, uint64_t user_id) {
    std::filesystem::path path = svc->upload_root / std::to_string(user_id);
    if (!ensure_directory(path)) {
        LOGE("Failed to ensure upload directory %s", path.string().c_str());
        return std::nullopt;
    }
    return path;
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
    auto user_dir = ensure_user_upload_dir(svc, user_id);
    if (!user_dir) {
        std::free(binary);
        return -1;
    }

    std::ostringstream name_stream;
    name_stream << util_now_ms() << '-' << util_rand64() << '-';
    if (payload->filename[0] != '\0') {
        name_stream << payload->filename;
    } else {
        name_stream << "attachment";
    }
    std::string filename = name_stream.str();
    std::filesystem::path fullpath = *user_dir / filename;

    std::ofstream out(fullpath, std::ios::binary);
    if (!out.is_open()) {
        std::free(binary);
        return -1;
    }
    out.write(reinterpret_cast<const char *>(binary), static_cast<std::streamsize>(binary_len));
    out.close();
    if (!out) {
        std::free(binary);
        return -1;
    }
    std::free(binary);

    memset(out_rec, 0, sizeof(*out_rec));
    util_strlcpy(out_rec->filename, sizeof(out_rec->filename), payload->filename);
    const std::string storage_path = fullpath.string();
    util_strlcpy(out_rec->storage_path, sizeof(out_rec->storage_path), storage_path.c_str());
    util_strlcpy(out_rec->mime_type, sizeof(out_rec->mime_type), payload->mime_type);
    util_strlcpy(out_rec->relative_path, sizeof(out_rec->relative_path), payload->relative_path);
    if (out_rec->relative_path[0] == '\0') {
        util_strlcpy(out_rec->relative_path, sizeof(out_rec->relative_path), filename.c_str());
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

    attachment_list_t attachments{};
    if (compose->attachment_count > 0) {
    attachments.items = static_cast<attachment_record_t*>(std::calloc(compose->attachment_count, sizeof(attachment_record_t)));
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
