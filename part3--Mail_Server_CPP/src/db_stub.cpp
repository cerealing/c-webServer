#ifndef USE_REAL_MYSQL
#include "db.h"
#include "logger.h"
#include "util.h"

#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <strings.h>

typedef struct {
    user_record_t *data;
    size_t size;
    size_t capacity;
} user_vec_t;

typedef struct {
    folder_record_t *data;
    size_t size;
    size_t capacity;
} folder_vec_t;

typedef struct {
    message_record_t *data;
    size_t size;
    size_t capacity;
} message_vec_t;

typedef struct {
    attachment_record_t *data;
    size_t size;
    size_t capacity;
} attachment_vec_t;

typedef struct {
    contact_record_t *data;
    size_t size;
    size_t capacity;
} contact_vec_t;

struct db_handle {
    pthread_mutex_t mutex;
    server_config config;

    user_vec_t users;
    folder_vec_t folders;
    message_vec_t messages;
    attachment_vec_t attachments;
    contact_vec_t contacts;

    uint64_t next_user_id;
    uint64_t next_folder_id;
    uint64_t next_message_id;
    uint64_t next_attachment_id;
    uint64_t next_contact_id;
};

static void *ensure_capacity(void **buf, size_t *capacity, size_t elem_size, size_t min_cap) {
    if (*capacity >= min_cap) return *buf;
    size_t new_cap = (*capacity == 0) ? 8 : *capacity;
    while (new_cap < min_cap) new_cap *= 2;
    void *tmp = std::realloc(*buf, new_cap * elem_size);
    if (!tmp) return NULL;
    *buf = tmp;
    *capacity = new_cap;
    return tmp;
}

static void stub_seed_messages_and_contacts(db_handle_t *db);

static void stub_seed_users(db_handle_t *db) {
    const struct {
        const char *username;
        const char *email;
        const char *password;
    } seeds[] = {
        {"alice", "alice@example.com", "alice123"},
        {"bob", "bob@example.com", "bob123"},
        {"carol", "carol@example.com", "carol123"},
        {"dave", "dave@example.com", "dave123"},
        {"eve", "eve@example.com", "eve123"}
    };

    size_t count = sizeof(seeds) / sizeof(seeds[0]);
    ensure_capacity((void **)&db->users.data, &db->users.capacity,
                    sizeof(user_record_t), db->users.size + count);

    time_t now = time(NULL);
    for (size_t i = 0; i < count; ++i) {
        user_record_t rec = {0};
        rec.id = ++db->next_user_id;
        strncpy(rec.username, seeds[i].username, sizeof(rec.username) - 1);
        strncpy(rec.email, seeds[i].email, sizeof(rec.email) - 1);
        strncpy(rec.password_hash, seeds[i].password, sizeof(rec.password_hash) - 1);
        rec.created_at = now - (time_t)(i * 3600);
        db->users.data[db->users.size++] = rec;
    }
}

static void stub_ensure_default_folders(db_handle_t *db, uint64_t user_id) {
    const folder_kind_t defaults[] = {
        FOLDER_INBOX, FOLDER_SENT, FOLDER_DRAFTS, FOLDER_STARRED, FOLDER_ARCHIVE
    };
    const char *names[] = {"Inbox", "Sent", "Drafts", "Starred", "Archive"};

    for (size_t i = 0; i < sizeof(defaults)/sizeof(defaults[0]); ++i) {
        int exists = 0;
        for (size_t j = 0; j < db->folders.size; ++j) {
            if (db->folders.data[j].owner_id == user_id &&
                db->folders.data[j].kind == defaults[i]) {
                exists = 1;
                break;
            }
        }
        if (!exists) {
            ensure_capacity((void **)&db->folders.data, &db->folders.capacity,
                            sizeof(folder_record_t), db->folders.size + 1);
            folder_record_t rec = {
                .id = ++db->next_folder_id,
                .owner_id = user_id,
                .kind = defaults[i],
                .created_at = time(NULL)
            };
            strncpy(rec.name, names[i], sizeof(rec.name)-1);
            db->folders.data[db->folders.size++] = rec;
        }
    }
}

int db_init(const server_config *cfg, db_handle_t **out) {
    db_handle_t *db = static_cast<db_handle_t*>(std::calloc(1, sizeof(*db)));
    if (!db) return -1;
    pthread_mutex_init(&db->mutex, NULL);
    if (cfg) db->config = *cfg;
    stub_seed_users(db);
    for (size_t i = 0; i < db->users.size; ++i) {
        stub_ensure_default_folders(db, db->users.data[i].id);
    }
    stub_seed_messages_and_contacts(db);
    *out = db;
    return 0;
}

void db_close(db_handle_t *db) {
    if (!db) return;
    pthread_mutex_destroy(&db->mutex);
    std::free(db->users.data);
    std::free(db->folders.data);
    std::free(db->messages.data);
    std::free(db->attachments.data);
    std::free(db->contacts.data);
    std::free(db);
}

static user_record_t *find_user_by_username(db_handle_t *db, const char *username) {
    for (size_t i = 0; i < db->users.size; ++i) {
        if (strcmp(db->users.data[i].username, username) == 0) {
            return &db->users.data[i];
        }
    }
    return NULL;
}

int db_authenticate(db_handle_t *db, const char *username, const char *password, user_record_t *out_user) {
    pthread_mutex_lock(&db->mutex);
    user_record_t *user = find_user_by_username(db, username);
    if (!user || strcmp(user->password_hash, password) != 0) {
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }
    if (out_user) *out_user = *user;
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

int db_get_user_by_id(db_handle_t *db, uint64_t user_id, user_record_t *out_user) {
    pthread_mutex_lock(&db->mutex);
    for (size_t i = 0; i < db->users.size; ++i) {
        if (db->users.data[i].id == user_id) {
            if (out_user) *out_user = db->users.data[i];
            pthread_mutex_unlock(&db->mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&db->mutex);
    return -1;
}

int db_get_user_by_username(db_handle_t *db, const char *username, user_record_t *out_user) {
    pthread_mutex_lock(&db->mutex);
    user_record_t *user = find_user_by_username(db, username);
    if (user && out_user) *out_user = *user;
    pthread_mutex_unlock(&db->mutex);
    return user ? 0 : -1;
}

int db_create_user(db_handle_t *db, const char *username, const char *email, const char *password, user_record_t *out_user) {
    if (!db || !username || !email || !password) return -1;
    pthread_mutex_lock(&db->mutex);
    if (find_user_by_username(db, username)) {
        pthread_mutex_unlock(&db->mutex);
        return DB_ERR_DUP_USERNAME;
    }
    for (size_t i = 0; i < db->users.size; ++i) {
        if (strcasecmp(db->users.data[i].email, email) == 0) {
            pthread_mutex_unlock(&db->mutex);
            return DB_ERR_DUP_EMAIL;
        }
    }
    if (!ensure_capacity((void **)&db->users.data, &db->users.capacity,
                         sizeof(user_record_t), db->users.size + 1)) {
        pthread_mutex_unlock(&db->mutex);
        return -1;
    }
    user_record_t rec = {0};
    rec.id = ++db->next_user_id;
    strncpy(rec.username, username, sizeof(rec.username) - 1);
    strncpy(rec.email, email, sizeof(rec.email) - 1);
    strncpy(rec.password_hash, password, sizeof(rec.password_hash) - 1);
    rec.created_at = time(NULL);
    db->users.data[db->users.size++] = rec;
    stub_ensure_default_folders(db, rec.id);
    if (out_user) *out_user = rec;
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

int db_list_folders(db_handle_t *db, uint64_t user_id, folder_list_t *out) {
    pthread_mutex_lock(&db->mutex);
    stub_ensure_default_folders(db, user_id);
    size_t count = 0;
    for (size_t i = 0; i < db->folders.size; ++i) {
        if (db->folders.data[i].owner_id == user_id) count++;
    }
    out->items = static_cast<folder_record_t*>(std::calloc(count, sizeof(folder_record_t)));
    out->count = count;
    size_t idx = 0;
    for (size_t i = 0; i < db->folders.size; ++i) {
        if (db->folders.data[i].owner_id == user_id) {
            out->items[idx++] = db->folders.data[i];
        }
    }
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

int db_create_folder(db_handle_t *db, uint64_t user_id, const char *name, folder_kind_t kind, folder_record_t *out_folder) {
    pthread_mutex_lock(&db->mutex);
    ensure_capacity((void **)&db->folders.data, &db->folders.capacity,
                    sizeof(folder_record_t), db->folders.size + 1);
    folder_record_t rec = {
        .id = ++db->next_folder_id,
        .owner_id = user_id,
        .kind = kind,
        .created_at = time(NULL)
    };
    strncpy(rec.name, name, sizeof(rec.name)-1);
    db->folders.data[db->folders.size++] = rec;
    if (out_folder) *out_folder = rec;
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

static int folder_match(const message_record_t *msg, folder_kind_t folder, const char *custom) {
    if (msg->folder != folder) return 0;
    if (folder == FOLDER_CUSTOM && custom) {
        return strcmp(msg->custom_folder, custom) == 0;
    }
    return 1;
}

int db_list_messages(db_handle_t *db, uint64_t user_id, folder_kind_t folder, const char *custom, message_list_t *out) {
    pthread_mutex_lock(&db->mutex);
    size_t count = 0;
    for (size_t i = 0; i < db->messages.size; ++i) {
        message_record_t *msg = &db->messages.data[i];
        if (msg->owner_id == user_id && folder_match(msg, folder, custom)) {
            count++;
        }
    }
    out->items = static_cast<message_record_t*>(std::calloc(count, sizeof(message_record_t)));
    out->count = count;
    size_t idx = 0;
    for (size_t i = 0; i < db->messages.size; ++i) {
        message_record_t *msg = &db->messages.data[i];
        if (msg->owner_id == user_id && folder_match(msg, folder, custom)) {
            out->items[idx++] = *msg;
        }
    }
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

int db_get_message(db_handle_t *db, uint64_t user_id, uint64_t message_id, message_record_t *out, attachment_list_t *attachments) {
    pthread_mutex_lock(&db->mutex);
    for (size_t i = 0; i < db->messages.size; ++i) {
        message_record_t *msg = &db->messages.data[i];
        if (msg->owner_id == user_id && msg->id == message_id) {
            if (out) *out = *msg;
            if (attachments) {
                size_t count = 0;
                for (size_t j = 0; j < db->attachments.size; ++j) {
                    if (db->attachments.data[j].message_id == msg->id) count++;
                }
                attachments->items = static_cast<attachment_record_t*>(std::calloc(count, sizeof(attachment_record_t)));
                attachments->count = count;
                size_t idx = 0;
                for (size_t j = 0; j < db->attachments.size; ++j) {
                    if (db->attachments.data[j].message_id == msg->id) {
                        attachments->items[idx++] = db->attachments.data[j];
                    }
                }
            }
            pthread_mutex_unlock(&db->mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&db->mutex);
    return -1;
}

static void copy_attachments(db_handle_t *db, uint64_t message_id, const attachment_list_t *attachments) {
    if (!attachments || attachments->count == 0) return;
    ensure_capacity((void **)&db->attachments.data, &db->attachments.capacity,
                    sizeof(attachment_record_t), db->attachments.size + attachments->count);
    for (size_t i = 0; i < attachments->count; ++i) {
        attachment_record_t rec = attachments->items[i];
        rec.id = ++db->next_attachment_id;
        rec.message_id = message_id;
        db->attachments.data[db->attachments.size++] = rec;
    }
}

int db_save_draft(db_handle_t *db, uint64_t user_id, message_record_t *msg, attachment_list_t *attachments) {
    pthread_mutex_lock(&db->mutex);
    msg->id = ++db->next_message_id;
    msg->owner_id = user_id;
    msg->folder = FOLDER_DRAFTS;
    msg->is_draft = 1;
    msg->created_at = msg->updated_at = time(NULL);
    ensure_capacity((void **)&db->messages.data, &db->messages.capacity,
                    sizeof(message_record_t), db->messages.size + 1);
    db->messages.data[db->messages.size++] = *msg;
    copy_attachments(db, msg->id, attachments);
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

static void add_message_copy(db_handle_t *db, const message_record_t *src, uint64_t owner_id,
                             folder_kind_t folder, const char *custom, const attachment_list_t *attachments) {
    ensure_capacity((void **)&db->messages.data, &db->messages.capacity,
                    sizeof(message_record_t), db->messages.size + 1);
    message_record_t copy = *src;
    copy.id = ++db->next_message_id;
    copy.owner_id = owner_id;
    copy.folder = folder;
    copy.is_draft = (folder == FOLDER_DRAFTS);
    copy.is_archived = (folder == FOLDER_ARCHIVE);
    copy.is_starred = (folder == FOLDER_STARRED);
    util_strlcpy(copy.archive_group, sizeof(copy.archive_group), src->archive_group);
    copy.created_at = copy.updated_at = time(NULL);
    if (custom) strncpy(copy.custom_folder, custom, sizeof(copy.custom_folder)-1);
    db->messages.data[db->messages.size++] = copy;
    if (attachments) copy_attachments(db, copy.id, attachments);
}

static uint64_t find_user_id_by_username(db_handle_t *db, const char *username) {
    for (size_t i = 0; i < db->users.size; ++i) {
        if (strcmp(db->users.data[i].username, username) == 0) {
            return db->users.data[i].id;
        }
    }
    return 0;
}

static message_record_t *stub_find_message(db_handle_t *db, uint64_t owner_id, const char *subject) {
    for (size_t i = 0; i < db->messages.size; ++i) {
        message_record_t *msg = &db->messages.data[i];
        if (msg->owner_id == owner_id && strcmp(msg->subject, subject) == 0) {
            return msg;
        }
    }
    return NULL;
}

static void stub_add_contact(db_handle_t *db, uint64_t user_id, uint64_t contact_user_id, const char *alias, const char *group_name) {
    if (!user_id || !contact_user_id) return;
    ensure_capacity((void **)&db->contacts.data, &db->contacts.capacity,
                    sizeof(contact_record_t), db->contacts.size + 1);
    contact_record_t rec = {0};
    rec.id = ++db->next_contact_id;
    rec.user_id = user_id;
    rec.contact_user_id = contact_user_id;
    if (alias && *alias) {
        util_strlcpy(rec.alias, sizeof(rec.alias), alias);
    } else {
        user_record_t contact_user = {0};
        if (db_get_user_by_id(db, contact_user_id, &contact_user) == 0) {
            util_strlcpy(rec.alias, sizeof(rec.alias), contact_user.username);
        }
    }
    if (group_name && *group_name) {
        util_strlcpy(rec.group_name, sizeof(rec.group_name), group_name);
    }
    rec.created_at = time(NULL) - (time_t)(db->contacts.size * 1800);
    db->contacts.data[db->contacts.size++] = rec;
}

static void stub_seed_messages_and_contacts(db_handle_t *db) {
    uint64_t alice = find_user_id_by_username(db, "alice");
    uint64_t bob = find_user_id_by_username(db, "bob");
    uint64_t carol = find_user_id_by_username(db, "carol");
    uint64_t dave = find_user_id_by_username(db, "dave");
    uint64_t eve = find_user_id_by_username(db, "eve");

    if (alice && bob) {
        message_record_t kickoff = {0};
        strncpy(kickoff.subject, "Project Kickoff", sizeof(kickoff.subject) - 1);
        strncpy(kickoff.body,
                "Team,\n\nWelcome to the Project Atlas kickoff. Please review the brief before tomorrow's sync.\n\n- Alice",
                sizeof(kickoff.body) - 1);
        strncpy(kickoff.recipients, "bob,carol", sizeof(kickoff.recipients) - 1);
        db_send_message(db, alice, &kickoff, NULL);

        message_record_t notes = {0};
        strncpy(notes.subject, "Atlas daily notes", sizeof(notes.subject) - 1);
        strncpy(notes.body,
                "Stand-up summary:\n- Backend API contract finalized\n- UI polishing still pending\n\nCheers, Alice",
                sizeof(notes.body) - 1);
        strncpy(notes.recipients, "bob,carol,dave", sizeof(notes.recipients) - 1);
        db_send_message(db, alice, &notes, NULL);
    }

    if (bob && alice) {
        message_record_t reply = {0};
        strncpy(reply.subject, "Re: Project Kickoff", sizeof(reply.subject) - 1);
        strncpy(reply.body,
                "Thanks Alice, attaching the revised timeline. Let's sync at 10am.\n\n- Bob",
                sizeof(reply.body) - 1);
        strncpy(reply.recipients, "alice", sizeof(reply.recipients) - 1);
        db_send_message(db, bob, &reply, NULL);
    }

    if (carol && alice) {
        message_record_t ux = {0};
        strncpy(ux.subject, "UX Review Checklist", sizeof(ux.subject) - 1);
        strncpy(ux.body,
                "Hi Alice,\n\nHere is the UX review checklist for sprint 5. Please share feedback by EOD.\n\nThanks, Carol",
                sizeof(ux.body) - 1);
        strncpy(ux.recipients, "alice", sizeof(ux.recipients) - 1);
        db_send_message(db, carol, &ux, NULL);
    }

    if (dave && eve) {
        message_record_t ops = {0};
        strncpy(ops.subject, "Ops Handoff", sizeof(ops.subject) - 1);
        strncpy(ops.body,
                "Eve,\n\nServers patched and dashboards updated. Let me know if you see any anomalies.\n\n- Dave",
                sizeof(ops.body) - 1);
        strncpy(ops.recipients, "eve", sizeof(ops.recipients) - 1);
        db_send_message(db, dave, &ops, NULL);
    }

    if (carol) {
        message_record_t draft = {0};
        strncpy(draft.subject, "Content ideas", sizeof(draft.subject) - 1);
        strncpy(draft.body,
                "Drafting newsletter topics: AI digest, release notes, customer spotlight.",
                sizeof(draft.body) - 1);
        strncpy(draft.recipients, "alice", sizeof(draft.recipients) - 1);
        db_save_draft(db, carol, &draft, NULL);
    }

    if (alice) {
        folder_record_t custom = {0};
        db_create_folder(db, alice, "Product", FOLDER_CUSTOM, &custom);
        ensure_capacity((void **)&db->messages.data, &db->messages.capacity,
                        sizeof(message_record_t), db->messages.size + 1);
        message_record_t bulletin = {0};
        bulletin.id = ++db->next_message_id;
        bulletin.owner_id = alice;
        bulletin.folder = FOLDER_CUSTOM;
    util_strlcpy(bulletin.custom_folder, sizeof(bulletin.custom_folder), custom.name);
        strncpy(bulletin.subject, "Weekly Product Bulletin", sizeof(bulletin.subject) - 1);
        strncpy(bulletin.body,
                "Feature gating complete. QA cycle starts Monday. This bulletin tracks remaining tasks.",
                sizeof(bulletin.body) - 1);
        strncpy(bulletin.recipients, "bob,carol", sizeof(bulletin.recipients) - 1);
        bulletin.created_at = time(NULL) - 5400;
        bulletin.updated_at = bulletin.created_at;
        db->messages.data[db->messages.size++] = bulletin;
    }

    // Promote one of the inbox messages to starred/archive for variety
    if (alice) {
        message_record_t *msg = stub_find_message(db, alice, "Re: Project Kickoff");
        if (msg) {
            db_star_message(db, alice, msg->id, 1);
        }
    }
    if (eve) {
        message_record_t *msg = stub_find_message(db, eve, "Ops Handoff");
        if (msg) {
            db_archive_message(db, eve, msg->id, 1, "历史邮件");
        }
    }

    // Contacts
    stub_add_contact(db, alice, bob, "Bob – Engineering", "工程");
    stub_add_contact(db, alice, carol, "Carol (Design)", "设计");
    stub_add_contact(db, bob, alice, "Alice PM", "管理");
    stub_add_contact(db, carol, dave, "Dave Ops", "运维");
    stub_add_contact(db, eve, dave, "Dave Support", "客服");
}

int db_send_message(db_handle_t *db, uint64_t user_id, const message_record_t *msg, const attachment_list_t *attachments) {
    pthread_mutex_lock(&db->mutex);
    message_record_t base = *msg;
    base.owner_id = user_id;
    base.folder = FOLDER_SENT;
    base.is_draft = 0;
    base.archive_group[0] = '\0';
    base.created_at = base.updated_at = time(NULL);
    add_message_copy(db, &base, user_id, FOLDER_SENT, NULL, attachments);

    char recipients_copy[RECIPIENT_MAX];
    strncpy(recipients_copy, msg->recipients, sizeof(recipients_copy)-1);
    recipients_copy[sizeof(recipients_copy)-1] = '\0';
    char *saveptr = NULL;
    char *token = strtok_r(recipients_copy, ",", &saveptr);
    while (token) {
        while (*token == ' ') token++;
        uint64_t rid = find_user_id_by_username(db, token);
        if (rid) {
            add_message_copy(db, &base, rid, FOLDER_INBOX, NULL, attachments);
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

int db_star_message(db_handle_t *db, uint64_t user_id, uint64_t message_id, int starred) {
    pthread_mutex_lock(&db->mutex);
    for (size_t i = 0; i < db->messages.size; ++i) {
        message_record_t *msg = &db->messages.data[i];
        if (msg->owner_id == user_id && msg->id == message_id) {
            msg->is_starred = starred;
            if (starred && msg->folder != FOLDER_STARRED) {
                add_message_copy(db, msg, user_id, FOLDER_STARRED, NULL, NULL);
            }
            pthread_mutex_unlock(&db->mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&db->mutex);
    return -1;
}

int db_archive_message(db_handle_t *db, uint64_t user_id, uint64_t message_id, int archived, const char *group_name) {
    pthread_mutex_lock(&db->mutex);
    for (size_t i = 0; i < db->messages.size; ++i) {
        message_record_t *msg = &db->messages.data[i];
        if (msg->owner_id == user_id && msg->id == message_id) {
            msg->is_archived = archived;
            msg->folder = archived ? FOLDER_ARCHIVE : FOLDER_INBOX;
            if (group_name) {
                util_strlcpy(msg->archive_group, sizeof(msg->archive_group), group_name);
            } else if (!archived) {
                msg->archive_group[0] = '\0';
            }
            pthread_mutex_unlock(&db->mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&db->mutex);
    return -1;
}

int db_list_contacts(db_handle_t *db, uint64_t user_id, contact_list_t *out) {
    pthread_mutex_lock(&db->mutex);
    size_t count = 0;
    for (size_t i = 0; i < db->contacts.size; ++i) {
        if (db->contacts.data[i].user_id == user_id) count++;
    }
    out->items = static_cast<contact_record_t*>(std::calloc(count, sizeof(contact_record_t)));
    out->count = count;
    size_t idx = 0;
    for (size_t i = 0; i < db->contacts.size; ++i) {
        if (db->contacts.data[i].user_id == user_id) {
            out->items[idx++] = db->contacts.data[i];
        }
    }
    pthread_mutex_unlock(&db->mutex);
    return 0;
}

int db_add_contact(db_handle_t *db, uint64_t user_id, const char *alias, const char *group_name, uint64_t contact_user_id, contact_record_t *out) {
    pthread_mutex_lock(&db->mutex);
    ensure_capacity((void **)&db->contacts.data, &db->contacts.capacity,
                    sizeof(contact_record_t), db->contacts.size + 1);
    contact_record_t rec = {
        .id = ++db->next_contact_id,
        .user_id = user_id,
        .contact_user_id = contact_user_id,
        .created_at = time(NULL)
    };
    util_strlcpy(rec.alias, sizeof(rec.alias), alias);
    if (group_name) {
        util_strlcpy(rec.group_name, sizeof(rec.group_name), group_name);
    }
    db->contacts.data[db->contacts.size++] = rec;
    if (out) *out = rec;
    pthread_mutex_unlock(&db->mutex);
    return 0;
}
#endif
