#ifdef USE_REAL_MYSQL
#include "db.h"
#include "logger.h"

#include <mysql/mysql.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <memory>
#include <ctype.h>

struct db_handle {
    mail::ServerConfig config;
    MYSQL **pool;
    unsigned char *busy;
    size_t pool_size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

static MYSQL *acquire_conn(db_handle_t *db) {
    pthread_mutex_lock(&db->mutex);
    while (1) {
        for (size_t i = 0; i < db->pool_size; ++i) {
            if (!db->busy[i]) {
                db->busy[i] = 1;
                MYSQL *conn = db->pool[i];
                pthread_mutex_unlock(&db->mutex);
                return conn;
            }
        }
        pthread_cond_wait(&db->cond, &db->mutex);
    }
}

static void release_conn(db_handle_t *db, MYSQL *conn) {
    if (!conn) return;
    pthread_mutex_lock(&db->mutex);
    for (size_t i = 0; i < db->pool_size; ++i) {
        if (db->pool[i] == conn) {
            db->busy[i] = 0;
            pthread_cond_signal(&db->cond);
            break;
        }
    }
    pthread_mutex_unlock(&db->mutex);
}

static unsigned long escape_dup(MYSQL *conn, const char *src, char **out) {
    size_t len = src ? strlen(src) : 0;
    *out = static_cast<char *>(malloc(len * 2 + 1));
    if (!*out) return 0;
    if (!src) {
        (*out)[0] = '\0';
        return 0;
    }
    unsigned long escaped = mysql_real_escape_string(conn, *out, src, len);
    (*out)[escaped] = '\0';
    return escaped;
}

static void ensure_column(MYSQL *conn, const char *sql) {
    if (mysql_query(conn, sql) != 0) {
        unsigned int err = mysql_errno(conn);
        if (err != 1060) {
            LOGW("mysql: %s failed: %s", sql, mysql_error(conn));
        }
    }
}

static void ensure_constraint(MYSQL *conn, const char *sql) {
    if (mysql_query(conn, sql) != 0) {
        unsigned int err = mysql_errno(conn);
        if (err != 1826 && err != 1061 && err != 1022) {
            LOGW("mysql: %s failed: %s", sql, mysql_error(conn));
        }
    }
}

static int username_to_user_id(db_handle_t *db, MYSQL *conn, const char *username, uint64_t *out_id);

static bool column_exists(MYSQL *conn, const char *table, const char *column) {
    char query[2048];
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '%s' AND COLUMN_NAME = '%s'",
             table, column);
    if (mysql_query(conn, query) != 0) {
        LOGE("mysql: column_exists failed: %s", mysql_error(conn));
        return false;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        LOGE("mysql: column_exists store_result failed: %s", mysql_error(conn));
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    bool exists = row && row[0] && strcmp(row[0], "0") != 0;
    mysql_free_result(res);
    return exists;
}

static int insert_message_recipients(db_handle_t *db, MYSQL *conn, uint64_t message_id, const char *recipients) {
    if (!recipients || !*recipients) {
        char cleanup[256];
        snprintf(cleanup, sizeof(cleanup),
                 "DELETE FROM message_recipients WHERE message_id=%llu",
                 (unsigned long long)message_id);
        mysql_query(conn, cleanup);
        return 0;
    }

    char cleanup[256];
    snprintf(cleanup, sizeof(cleanup),
             "DELETE FROM message_recipients WHERE message_id=%llu",
             (unsigned long long)message_id);
    mysql_query(conn, cleanup);

    char buffer[RECIPIENT_MAX];
    strncpy(buffer, recipients, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(buffer, ",", &saveptr);
    while (token) {
        while (*token && isspace((unsigned char)*token)) token++;
        size_t len = strlen(token);
        while (len > 0 && isspace((unsigned char)token[len - 1])) {
            token[--len] = '\0';
        }
        if (*token) {
            char *esc_name = NULL;
            escape_dup(conn, token, &esc_name);
            uint64_t rid = 0;
            int has_user = username_to_user_id(db, conn, token, &rid);
            char query[512];
            if (has_user == 0) {
                snprintf(query, sizeof(query),
                         "INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username) "
                         "VALUES (%llu, %llu, '%s') "
                         "ON DUPLICATE KEY UPDATE recipient_user_id=VALUES(recipient_user_id)",
                         (unsigned long long)message_id,
                         (unsigned long long)rid,
                         esc_name ? esc_name : "");
            } else {
                snprintf(query, sizeof(query),
                         "INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username) "
                         "VALUES (%llu, NULL, '%s') "
                         "ON DUPLICATE KEY UPDATE recipient_username=VALUES(recipient_username)",
                         (unsigned long long)message_id,
                         esc_name ? esc_name : "");
            }
            if (mysql_query(conn, query) != 0) {
                LOGE("mysql: insert message recipient failed: %s", mysql_error(conn));
                free(esc_name);
                return -1;
            }
            free(esc_name);
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    return 0;
}

static int migrate_recipients_column(db_handle_t *db, MYSQL *conn) {
    if (!column_exists(conn, "messages", "recipients")) {
        return 0;
    }

    if (mysql_query(conn, "SELECT id, recipients FROM messages WHERE recipients IS NOT NULL AND recipients <> ''") != 0) {
        LOGE("mysql: migrate recipients select failed: %s", mysql_error(conn));
        return -1;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        LOGE("mysql: migrate recipients store_result failed: %s", mysql_error(conn));
        return -1;
    }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL) {
        uint64_t mid = (uint64_t)strtoull(row[0], NULL, 10);
        const char *rcpt = row[1] ? row[1] : "";
        if (insert_message_recipients(db, conn, mid, rcpt) != 0) {
            mysql_free_result(res);
            return -1;
        }
    }
    mysql_free_result(res);

    if (mysql_query(conn, "ALTER TABLE messages DROP COLUMN recipients") != 0) {
        unsigned int err = mysql_errno(conn);
        if (err != 1091) {
            LOGE("mysql: drop recipients column failed: %s", mysql_error(conn));
            return -1;
        }
    }
    return 0;
}

static void fill_user_row(MYSQL_ROW row, user_record_t *out) {
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)strtoull(row[0], NULL, 10);
    strncpy(out->username, row[1], sizeof(out->username) - 1);
    strncpy(out->email, row[2], sizeof(out->email) - 1);
    strncpy(out->password_hash, row[3], sizeof(out->password_hash) - 1);
    out->created_at = (time_t)strtoll(row[4], NULL, 10);
}

static void fill_folder_row(MYSQL_ROW row, folder_record_t *out) {
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)strtoull(row[0], NULL, 10);
    out->owner_id = (uint64_t)strtoull(row[1], NULL, 10);
    out->kind = (folder_kind_t)atoi(row[2]);
    strncpy(out->name, row[3], sizeof(out->name) - 1);
    out->created_at = (time_t)strtoll(row[4], NULL, 10);
}

static void fill_message_row(MYSQL_ROW row, message_record_t *out) {
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)strtoull(row[0], NULL, 10);
    out->owner_id = (uint64_t)strtoull(row[1], NULL, 10);
    out->folder = (folder_kind_t)atoi(row[2]);
    strncpy(out->custom_folder, row[3], sizeof(out->custom_folder) - 1);
    strncpy(out->archive_group, row[4], sizeof(out->archive_group) - 1);
    strncpy(out->subject, row[5], sizeof(out->subject) - 1);
    strncpy(out->body, row[6], sizeof(out->body) - 1);
    if (row[7]) {
        strncpy(out->recipients, row[7], sizeof(out->recipients) - 1);
    } else {
        out->recipients[0] = '\0';
    }
    out->is_starred = atoi(row[8]);
    out->is_draft = atoi(row[9]);
    out->is_archived = atoi(row[10]);
    out->created_at = (time_t)strtoll(row[11], NULL, 10);
    out->updated_at = (time_t)strtoll(row[12], NULL, 10);
}

static void fill_attachment_row(MYSQL_ROW row, attachment_record_t *out) {
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)strtoull(row[0], NULL, 10);
    out->message_id = (uint64_t)strtoull(row[1], NULL, 10);
    strncpy(out->filename, row[2], sizeof(out->filename) - 1);
    strncpy(out->storage_path, row[3], sizeof(out->storage_path) - 1);
    strncpy(out->relative_path, row[4], sizeof(out->relative_path) - 1);
    strncpy(out->mime_type, row[5], sizeof(out->mime_type) - 1);
    out->size_bytes = (uint64_t)strtoull(row[6], NULL, 10);
}

static void fill_contact_row(MYSQL_ROW row, contact_record_t *out) {
    memset(out, 0, sizeof(*out));
    out->id = (uint64_t)strtoull(row[0], NULL, 10);
    out->user_id = (uint64_t)strtoull(row[1], NULL, 10);
    out->contact_user_id = (uint64_t)strtoull(row[2], NULL, 10);
    strncpy(out->alias, row[3], sizeof(out->alias) - 1);
    strncpy(out->group_name, row[4], sizeof(out->group_name) - 1);
    out->created_at = (time_t)strtoll(row[5], NULL, 10);
}

static uint64_t mysql_insert_id64(MYSQL *conn) {
    return (uint64_t)mysql_insert_id(conn);
}

static int ensure_default_folders(db_handle_t *db, MYSQL *conn, uint64_t user_id) {
    char query[256];
    snprintf(query, sizeof(query),
             "INSERT IGNORE INTO folders (owner_id, kind, name) VALUES "
             "(%llu,0,'Inbox'),(%llu,1,'Sent'),(%llu,2,'Drafts'),(%llu,3,'Starred'),(%llu,4,'Archive')",
             (unsigned long long)user_id,
             (unsigned long long)user_id,
             (unsigned long long)user_id,
             (unsigned long long)user_id,
             (unsigned long long)user_id);
    if (mysql_query(conn, query) != 0) {
        LOGE("mysql: ensure_default_folders failed: %s", mysql_error(conn));
        return -1;
    }
    (void)db;
    return 0;
}

int db_init(const mail::ServerConfig &cfg, db_handle_t **out) {
    if (!out) return -1;
    if (mysql_library_init(0, NULL, NULL) != 0) {
        LOGF("mysql: library init failed");
        return -1;
    }

    std::unique_ptr<db_handle_t> db = std::make_unique<db_handle_t>();
    db->config = cfg;
    db->pool_size = cfg.mysql.pool_size > 0 ? static_cast<size_t>(cfg.mysql.pool_size) : 4;
    db->pool = static_cast<MYSQL **>(calloc(db->pool_size, sizeof(MYSQL *)));
    db->busy = static_cast<unsigned char *>(calloc(db->pool_size, 1));
    if (!db->pool || !db->busy) {
        free(db->pool);
        free(db->busy);
        mysql_library_end();
        return -1;
    }
    pthread_mutex_init(&db->mutex, NULL);
    pthread_cond_init(&db->cond, NULL);

    for (size_t i = 0; i < db->pool_size; ++i) {
        MYSQL *conn = mysql_init(NULL);
        if (!conn) {
            LOGF("mysql: init failed");
            db_close(db.release());
            return -1;
        }
        mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");
        if (!mysql_real_connect(conn,
                                cfg.mysql.host.c_str(),
                                cfg.mysql.user.c_str(),
                                cfg.mysql.password.c_str(),
                                cfg.mysql.database.c_str(),
                                cfg.mysql.port,
                                NULL,
                                CLIENT_MULTI_STATEMENTS)) {
            LOGF("mysql: connect failed: %s", mysql_error(conn));
            mysql_close(conn);
            db_close(db.release());
            return -1;
        }
        mysql_query(conn, "SET time_zone = '+00:00'");
        db->pool[i] = conn;
    }

    MYSQL *conn = acquire_conn(db.get());
    if (!conn) {
        db_close(db.release());
        return -1;
    }
    if (mysql_query(conn, "CREATE TABLE IF NOT EXISTS users ("
                         "id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,"
                         "username VARCHAR(64) UNIQUE NOT NULL,"
                         "email VARCHAR(128) UNIQUE NOT NULL,"
                         "password_hash VARCHAR(128) NOT NULL,"
                         "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP"
                         ")") != 0) {
        LOGF("mysql: create users failed: %s", mysql_error(conn));
        release_conn(db.get(), conn);
        db_close(db.release());
        return -1;
    }
    if (mysql_query(conn, "CREATE TABLE IF NOT EXISTS folders ("
                         "id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,"
                         "owner_id BIGINT UNSIGNED NOT NULL,"
                         "kind INT NOT NULL,"
                         "name VARCHAR(64) NOT NULL,"
                         "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                         "UNIQUE KEY unique_folder(owner_id, kind, name)"
                         ")") != 0) {
        LOGF("mysql: create folders failed: %s", mysql_error(conn));
        release_conn(db.get(), conn);
        db_close(db.release());
        return -1;
    }
    if (mysql_query(conn, "CREATE TABLE IF NOT EXISTS messages ("
                         "id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,"
                         "owner_id BIGINT UNSIGNED NOT NULL,"
                         "folder INT NOT NULL,"
                         "custom_folder VARCHAR(64) NOT NULL DEFAULT '',"
                         "archive_group VARCHAR(64) NOT NULL DEFAULT '',"
                         "subject VARCHAR(256) NOT NULL,"
                         "body MEDIUMTEXT NOT NULL,"
                         "is_starred TINYINT(1) NOT NULL DEFAULT 0,"
                         "is_draft TINYINT(1) NOT NULL DEFAULT 0,"
                         "is_archived TINYINT(1) NOT NULL DEFAULT 0,"
                         "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                         "updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
                         ")") != 0) {
        LOGF("mysql: create messages failed: %s", mysql_error(conn));
        release_conn(db.get(), conn);
        db_close(db.release());
        return -1;
    }
    if (mysql_query(conn, "CREATE TABLE IF NOT EXISTS attachments ("
                         "id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,"
                         "message_id BIGINT UNSIGNED NOT NULL,"
                         "filename VARCHAR(256) NOT NULL,"
                         "storage_path VARCHAR(512) NOT NULL,"
                         "relative_path VARCHAR(256) NOT NULL DEFAULT '',"
                         "mime_type VARCHAR(128) NOT NULL,"
                         "size_bytes BIGINT UNSIGNED NOT NULL,"
                         "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                         "KEY message_idx(message_id)"
                         ")") != 0) {
        LOGF("mysql: create attachments failed: %s", mysql_error(conn));
        release_conn(db.get(), conn);
        db_close(db.release());
        return -1;
    }
    if (mysql_query(conn, "CREATE TABLE IF NOT EXISTS contacts ("
                         "id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,"
                         "user_id BIGINT UNSIGNED NOT NULL,"
                         "contact_user_id BIGINT UNSIGNED NOT NULL,"
                         "alias VARCHAR(128) NOT NULL,"
                         "group_name VARCHAR(64) NOT NULL DEFAULT '',"
                         "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                         "UNIQUE KEY uniq_contact(user_id, contact_user_id)"
                         ")") != 0) {
        LOGF("mysql: create contacts failed: %s", mysql_error(conn));
        release_conn(db.get(), conn);
        db_close(db.release());
        return -1;
    }
    if (mysql_query(conn, "CREATE TABLE IF NOT EXISTS message_recipients ("
                         "id BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,"
                         "message_id BIGINT UNSIGNED NOT NULL,"
                         "recipient_user_id BIGINT UNSIGNED NULL,"
                         "recipient_username VARCHAR(64) NOT NULL,"
                         "created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                         "UNIQUE KEY uniq_message_recipient(message_id, recipient_username),"
                         "KEY idx_message_recipient_user(recipient_user_id)"
                         ")") != 0) {
        LOGF("mysql: create message_recipients failed: %s", mysql_error(conn));
        release_conn(db.get(), conn);
        db_close(db.release());
        return -1;
    }
    ensure_constraint(conn, "ALTER TABLE message_recipients ADD CONSTRAINT fk_recipient_message FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE");
    ensure_constraint(conn, "ALTER TABLE message_recipients ADD CONSTRAINT fk_recipient_user FOREIGN KEY (recipient_user_id) REFERENCES users(id) ON DELETE SET NULL");
    ensure_column(conn, "ALTER TABLE messages ADD COLUMN archive_group VARCHAR(64) NOT NULL DEFAULT '' AFTER custom_folder");
    ensure_column(conn, "ALTER TABLE attachments ADD COLUMN relative_path VARCHAR(256) NOT NULL DEFAULT '' AFTER storage_path");
    ensure_column(conn, "ALTER TABLE contacts ADD COLUMN group_name VARCHAR(64) NOT NULL DEFAULT '' AFTER alias");
    migrate_recipients_column(db.get(), conn);
    release_conn(db.get(), conn);

    *out = db.release();
    return 0;
}

void db_close(db_handle_t *db) {
    if (!db) return;
    for (size_t i = 0; i < db->pool_size; ++i) {
        if (db->pool[i]) {
            mysql_close(db->pool[i]);
        }
    }
    pthread_mutex_destroy(&db->mutex);
    pthread_cond_destroy(&db->cond);
    free(db->busy);
    free(db->pool);
    delete db;
    mysql_library_end();
}

int db_authenticate(db_handle_t *db, const char *username, const char *password, user_record_t *out_user) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    char *esc_user = NULL;
    escape_dup(conn, username, &esc_user);
    char query[2048];
    snprintf(query, sizeof(query),
             "SELECT id, username, email, password_hash, UNIX_TIMESTAMP(created_at) "
             "FROM users WHERE username='%s' LIMIT 1",
             esc_user ? esc_user : "");
    int rc = -1;
    if (mysql_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && strcmp(row[3], password) == 0) {
            if (out_user) fill_user_row(row, out_user);
            rc = 0;
        }
        mysql_free_result(res);
    } else {
        LOGE("mysql: auth query failed: %s", mysql_error(conn));
    }
    free(esc_user);
    release_conn(db, conn);
    return rc;
}

int db_get_user_by_id(db_handle_t *db, uint64_t user_id, user_record_t *out_user) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    char query[256];
    snprintf(query, sizeof(query),
             "SELECT id, username, email, password_hash, UNIX_TIMESTAMP(created_at) "
             "FROM users WHERE id=%llu",
             (unsigned long long)user_id);
    int rc = -1;
    if (mysql_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            if (out_user) fill_user_row(row, out_user);
            rc = 0;
        }
        mysql_free_result(res);
    } else {
        LOGE("mysql: get_user_by_id failed: %s", mysql_error(conn));
    }
    release_conn(db, conn);
    return rc;
}

int db_get_user_by_username(db_handle_t *db, const char *username, user_record_t *out_user) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    char *esc_user = NULL;
    escape_dup(conn, username, &esc_user);
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT id, username, email, password_hash, UNIX_TIMESTAMP(created_at) "
             "FROM users WHERE username='%s'",
             esc_user ? esc_user : "");
    int rc = -1;
    if (mysql_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            if (out_user) fill_user_row(row, out_user);
            rc = 0;
        }
        mysql_free_result(res);
    } else {
        LOGE("mysql: get_user_by_username failed: %s", mysql_error(conn));
    }
    free(esc_user);
    release_conn(db, conn);
    return rc;
}

int db_create_user(db_handle_t *db, const char *username, const char *email, const char *password, user_record_t *out_user) {
    if (!db || !username || !email || !password) return -1;
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    char *esc_user = NULL;
    char *esc_email = NULL;
    char *esc_pass = NULL;
    escape_dup(conn, username, &esc_user);
    escape_dup(conn, email, &esc_email);
    escape_dup(conn, password, &esc_pass);
    char query[768];
    snprintf(query, sizeof(query),
             "INSERT INTO users (username, email, password_hash, created_at) VALUES ('%s','%s','%s',NOW())",
             esc_user ? esc_user : "",
             esc_email ? esc_email : "",
             esc_pass ? esc_pass : "");
    int rc = 0;
    if (mysql_query(conn, query) != 0) {
        unsigned int err = mysql_errno(conn);
        if (err == 1062) {
            const char *msg = mysql_error(conn);
            if (msg && strstr(msg, "username")) {
                rc = DB_ERR_DUP_USERNAME;
            } else if (msg && strstr(msg, "email")) {
                rc = DB_ERR_DUP_EMAIL;
            } else {
                rc = DB_ERR_DUP_USERNAME;
            }
        } else {
            LOGE("mysql: create_user failed: %s", mysql_error(conn));
            rc = -1;
        }
    } else {
        uint64_t new_id = mysql_insert_id64(conn);
        ensure_default_folders(db, conn, new_id);
        if (out_user) {
            memset(out_user, 0, sizeof(*out_user));
            out_user->id = new_id;
            strncpy(out_user->username, username, sizeof(out_user->username) - 1);
            strncpy(out_user->email, email, sizeof(out_user->email) - 1);
            strncpy(out_user->password_hash, password, sizeof(out_user->password_hash) - 1);
            out_user->created_at = time(NULL);
        }
    }
    free(esc_user);
    free(esc_email);
    free(esc_pass);
    release_conn(db, conn);
    return rc;
}

int db_list_folders(db_handle_t *db, uint64_t user_id, folder_list_t *out) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    if (!out) {
        release_conn(db, conn);
        return -1;
    }
    out->items = NULL;
    out->count = 0;
    ensure_default_folders(db, conn, user_id);
    char query[256];
    snprintf(query, sizeof(query),
             "SELECT id, owner_id, kind, name, UNIX_TIMESTAMP(created_at) "
             "FROM folders WHERE owner_id=%llu ORDER BY id",
             (unsigned long long)user_id);
    int rc = -1;
    if (mysql_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        size_t rows = mysql_num_rows(res);
    out->items = static_cast<folder_record_t *>(calloc(rows, sizeof(folder_record_t)));
        out->count = rows;
        size_t idx = 0;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != NULL) {
            fill_folder_row(row, &out->items[idx++]);
        }
        mysql_free_result(res);
        rc = 0;
    } else {
        LOGE("mysql: list_folders failed: %s", mysql_error(conn));
    }
    release_conn(db, conn);
    return rc;
}

int db_create_folder(db_handle_t *db, uint64_t user_id, const char *name, folder_kind_t kind, folder_record_t *out_folder) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    char *esc_name = NULL;
    escape_dup(conn, name, &esc_name);
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO folders (owner_id, kind, name) VALUES (%llu, %d, '%s')",
             (unsigned long long)user_id, kind, esc_name ? esc_name : "");
    int rc = -1;
    if (mysql_query(conn, query) == 0) {
        if (out_folder) {
            out_folder->id = mysql_insert_id64(conn);
            out_folder->owner_id = user_id;
            out_folder->kind = kind;
            strncpy(out_folder->name, name, sizeof(out_folder->name) - 1);
            out_folder->created_at = time(NULL);
        }
        rc = 0;
    } else {
        LOGE("mysql: create_folder failed: %s", mysql_error(conn));
    }
    free(esc_name);
    release_conn(db, conn);
    return rc;
}

static int list_messages_internal(MYSQL *conn, uint64_t user_id, folder_kind_t folder, const char *custom, message_list_t *out) {
    char query[1024];
    if (folder == FOLDER_CUSTOM && custom && *custom) {
        char *esc_custom = NULL;
        escape_dup(conn, custom, &esc_custom);
        snprintf(query, sizeof(query),
                 "SELECT m.id, m.owner_id, m.folder, m.custom_folder, m.archive_group, m.subject, m.body, "
                 "COALESCE(r.recipients, '') AS recipients, "
                 "m.is_starred, m.is_draft, m.is_archived, "
                 "UNIX_TIMESTAMP(m.created_at), UNIX_TIMESTAMP(m.updated_at) "
                 "FROM messages m "
                 "LEFT JOIN (SELECT message_id, GROUP_CONCAT(recipient_username ORDER BY recipient_username SEPARATOR ',') AS recipients "
                 "           FROM message_recipients GROUP BY message_id) r ON r.message_id = m.id "
                 "WHERE m.owner_id=%llu AND m.folder=%d AND m.custom_folder='%s' "
                 "ORDER BY m.updated_at DESC",
                 (unsigned long long)user_id, folder, esc_custom ? esc_custom : "");
        free(esc_custom);
    } else {
        snprintf(query, sizeof(query),
                 "SELECT m.id, m.owner_id, m.folder, m.custom_folder, m.archive_group, m.subject, m.body, "
                 "COALESCE(r.recipients, '') AS recipients, "
                 "m.is_starred, m.is_draft, m.is_archived, "
                 "UNIX_TIMESTAMP(m.created_at), UNIX_TIMESTAMP(m.updated_at) "
                 "FROM messages m "
                 "LEFT JOIN (SELECT message_id, GROUP_CONCAT(recipient_username ORDER BY recipient_username SEPARATOR ',') AS recipients "
                 "           FROM message_recipients GROUP BY message_id) r ON r.message_id = m.id "
                 "WHERE m.owner_id=%llu AND m.folder=%d "
                 "ORDER BY m.updated_at DESC",
                 (unsigned long long)user_id, folder);
    }
    if (mysql_query(conn, query) != 0) {
        LOGE("mysql: list_messages failed: %s", mysql_error(conn));
        return -1;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    size_t rows = mysql_num_rows(res);
    out->items = static_cast<message_record_t *>(calloc(rows, sizeof(message_record_t)));
    out->count = rows;
    size_t idx = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL) {
        fill_message_row(row, &out->items[idx++]);
    }
    mysql_free_result(res);
    return 0;
}

int db_list_messages(db_handle_t *db, uint64_t user_id, folder_kind_t folder, const char *custom, message_list_t *out) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    if (!out) {
        release_conn(db, conn);
        return -1;
    }
    out->items = NULL;
    out->count = 0;
    int rc = list_messages_internal(conn, user_id, folder, custom, out);
    release_conn(db, conn);
    return rc;
}

int db_get_message(db_handle_t *db, uint64_t user_id, uint64_t message_id, message_record_t *out, attachment_list_t *attachments) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    if (!out) {
        release_conn(db, conn);
        return -1;
    }
    if (attachments) {
        attachments->items = NULL;
        attachments->count = 0;
    }
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT m.id, m.owner_id, m.folder, m.custom_folder, m.archive_group, m.subject, m.body, "
             "COALESCE(r.recipients, '') AS recipients, "
             "m.is_starred, m.is_draft, m.is_archived, "
             "UNIX_TIMESTAMP(m.created_at), UNIX_TIMESTAMP(m.updated_at) "
             "FROM messages m "
             "LEFT JOIN (SELECT message_id, GROUP_CONCAT(recipient_username ORDER BY recipient_username SEPARATOR ',') AS recipients "
             "           FROM message_recipients GROUP BY message_id) r ON r.message_id = m.id "
             "WHERE m.owner_id=%llu AND m.id=%llu",
             (unsigned long long)user_id, (unsigned long long)message_id);
    int rc = -1;
    if (mysql_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            if (out) fill_message_row(row, out);
            rc = 0;
        }
        mysql_free_result(res);
        if (rc == 0 && attachments) {
            snprintf(query, sizeof(query),
                     "SELECT id, message_id, filename, storage_path, relative_path, mime_type, size_bytes "
                     "FROM attachments WHERE message_id=%llu",
                     (unsigned long long)message_id);
            if (mysql_query(conn, query) == 0) {
                MYSQL_RES *ares = mysql_store_result(conn);
                size_t rows = mysql_num_rows(ares);
                attachments->items = static_cast<attachment_record_t *>(calloc(rows, sizeof(attachment_record_t)));
                attachments->count = rows;
                size_t idx = 0;
                MYSQL_ROW arow;
                while ((arow = mysql_fetch_row(ares)) != NULL) {
                    fill_attachment_row(arow, &attachments->items[idx++]);
                }
                mysql_free_result(ares);
            }
        }
    } else {
        LOGE("mysql: get_message failed: %s", mysql_error(conn));
    }
    release_conn(db, conn);
    return rc;
}

static int insert_message_with_attachments(db_handle_t *db, MYSQL *conn, uint64_t owner_id, const message_record_t *msg, const attachment_list_t *attachments, uint64_t *new_id) {
    char *esc_subject = NULL;
    char *esc_body = NULL;
    char *esc_custom = NULL;
    char *esc_group = NULL;
    escape_dup(conn, msg->subject, &esc_subject);
    escape_dup(conn, msg->body, &esc_body);
    escape_dup(conn, msg->custom_folder, &esc_custom);
    escape_dup(conn, msg->archive_group, &esc_group);

    char query[4096];
    uint64_t mid = 0;
    int rc = -1;

    snprintf(query, sizeof(query),
             "INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body, is_starred, is_draft, is_archived) "
             "VALUES (%llu, %d, '%s', '%s', '%s', '%s', %d, %d, %d)",
             (unsigned long long)owner_id,
             msg->folder,
             esc_custom ? esc_custom : "",
             esc_group ? esc_group : "",
             esc_subject ? esc_subject : "",
             esc_body ? esc_body : "",
             msg->is_starred,
             msg->is_draft,
             msg->is_archived);

    if (mysql_query(conn, query) == 0) {
        mid = mysql_insert_id64(conn);
        if (insert_message_recipients(db, conn, mid, msg->recipients) != 0) {
            LOGE("mysql: insert message recipients failed for message %llu", (unsigned long long)mid);
            goto done;
        }
        if (attachments && attachments->count > 0) {
            for (size_t i = 0; i < attachments->count; ++i) {
                const attachment_record_t *att = &attachments->items[i];
                char *esc_filename = NULL;
                char *esc_path = NULL;
                char *esc_rel = NULL;
                char *esc_mime = NULL;
                escape_dup(conn, att->filename, &esc_filename);
                escape_dup(conn, att->storage_path, &esc_path);
                escape_dup(conn, att->relative_path, &esc_rel);
                escape_dup(conn, att->mime_type, &esc_mime);
                snprintf(query, sizeof(query),
                         "INSERT INTO attachments (message_id, filename, storage_path, relative_path, mime_type, size_bytes) "
                         "VALUES (%llu, '%s', '%s', '%s', '%s', %llu)",
                         (unsigned long long)mid,
                         esc_filename ? esc_filename : "",
                         esc_path ? esc_path : "",
                         esc_rel ? esc_rel : "",
                         esc_mime ? esc_mime : "",
                         (unsigned long long)att->size_bytes);
                if (mysql_query(conn, query) != 0) {
                    LOGE("mysql: insert attachment failed: %s", mysql_error(conn));
                    free(esc_filename);
                    free(esc_path);
                    free(esc_rel);
                    free(esc_mime);
                    goto done;
                }
                free(esc_filename);
                free(esc_path);
                free(esc_rel);
                free(esc_mime);
            }
        }
        if (new_id) *new_id = mid;
        rc = 0;
    } else {
        LOGE("mysql: insert message failed: %s", mysql_error(conn));
    }

done:
    free(esc_subject);
    free(esc_body);
    free(esc_custom);
    free(esc_group);
    if (rc != 0 && mid != 0) {
        snprintf(query, sizeof(query), "DELETE FROM attachments WHERE message_id=%llu", (unsigned long long)mid);
        mysql_query(conn, query);
        snprintf(query, sizeof(query), "DELETE FROM messages WHERE id=%llu", (unsigned long long)mid);
        mysql_query(conn, query);
        snprintf(query, sizeof(query), "DELETE FROM message_recipients WHERE message_id=%llu", (unsigned long long)mid);
        mysql_query(conn, query);
    }
    return rc;
}

static int username_to_user_id(db_handle_t *db, MYSQL *conn, const char *username, uint64_t *out_id) {
    char *esc = NULL;
    escape_dup(conn, username, &esc);
    char query[512];
    snprintf(query, sizeof(query), "SELECT id FROM users WHERE username='%s'", esc ? esc : "");
    int rc = -1;
    if (mysql_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            *out_id = (uint64_t)strtoull(row[0], NULL, 10);
            rc = 0;
        }
        mysql_free_result(res);
    }
    free(esc);
    if (rc != 0) {
        LOGW("mysql: recipient %s not found", username);
    }
    (void)db;
    return rc;
}

int db_save_draft(db_handle_t *db, uint64_t user_id, message_record_t *msg, attachment_list_t *attachments) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    msg->folder = FOLDER_DRAFTS;
    msg->is_draft = 1;
    msg->is_archived = 0;
    msg->owner_id = user_id;
    msg->custom_folder[0] = '\0';
    int rc = insert_message_with_attachments(db, conn, user_id, msg, attachments, &msg->id);
    release_conn(db, conn);
    return rc;
}

int db_send_message(db_handle_t *db, uint64_t user_id, const message_record_t *msg, const attachment_list_t *attachments) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    if (mysql_query(conn, "START TRANSACTION") != 0) {
        LOGE("mysql: could not start transaction: %s", mysql_error(conn));
        release_conn(db, conn);
        return -1;
    }

    message_record_t base = *msg;
    base.folder = FOLDER_SENT;
    base.is_draft = 0;
    base.is_archived = 0;
    base.is_starred = msg->is_starred;
    base.owner_id = user_id;
    base.custom_folder[0] = '\0';
    base.archive_group[0] = '\0';

    bool failed = false;
    if (insert_message_with_attachments(db, conn, user_id, &base, attachments, NULL) != 0) {
        failed = true;
    }

    char recipients_copy[RECIPIENT_MAX];
    if (!failed) {
        strncpy(recipients_copy, msg->recipients, sizeof(recipients_copy) - 1);
        recipients_copy[sizeof(recipients_copy) - 1] = '\0';
        char *saveptr = NULL;
        char *token = strtok_r(recipients_copy, ",", &saveptr);
        while (token && !failed) {
            while (*token == ' ') token++;
            if (*token) {
                uint64_t rid = 0;
                if (username_to_user_id(db, conn, token, &rid) == 0) {
                    ensure_default_folders(db, conn, rid);
                    message_record_t copy = base;
                    copy.owner_id = rid;
                    copy.folder = FOLDER_INBOX;
                    copy.is_starred = 0;
                    copy.archive_group[0] = '\0';
                    copy.is_archived = 0;
                    copy.is_draft = 0;
                    copy.custom_folder[0] = '\0';
                    if (insert_message_with_attachments(db, conn, rid, &copy, attachments, NULL) != 0) {
                        failed = true;
                        break;
                    }
                }
            }
            token = strtok_r(NULL, ",", &saveptr);
        }
    }

    if (failed) {
        mysql_query(conn, "ROLLBACK");
        release_conn(db, conn);
        return -1;
    }

    mysql_query(conn, "COMMIT");
    release_conn(db, conn);
    return 0;
}

int db_star_message(db_handle_t *db, uint64_t user_id, uint64_t message_id, int starred) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE messages SET is_starred=%d WHERE owner_id=%llu AND id=%llu",
             starred ? 1 : 0,
             (unsigned long long)user_id,
             (unsigned long long)message_id);
    int rc = mysql_query(conn, query) == 0 ? 0 : -1;
    if (rc != 0) LOGE("mysql: star_message failed: %s", mysql_error(conn));
    release_conn(db, conn);
    return rc;
}

int db_archive_message(db_handle_t *db, uint64_t user_id, uint64_t message_id, int archived, const char *group_name) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    folder_kind_t folder = archived ? FOLDER_ARCHIVE : FOLDER_INBOX;
    char *esc_group = NULL;
    if (archived && group_name) {
        escape_dup(conn, group_name, &esc_group);
    }
    const char *group_value = archived ? (esc_group ? esc_group : "") : "";
    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE messages SET is_archived=%d, folder=%d, archive_group='%s' WHERE owner_id=%llu AND id=%llu",
             archived ? 1 : 0,
             folder,
             group_value,
             (unsigned long long)user_id,
             (unsigned long long)message_id);
    int rc = mysql_query(conn, query) == 0 ? 0 : -1;
    if (rc != 0) LOGE("mysql: archive_message failed: %s", mysql_error(conn));
    free(esc_group);
    release_conn(db, conn);
    return rc;
}

int db_list_contacts(db_handle_t *db, uint64_t user_id, contact_list_t *out) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    if (!out) {
        release_conn(db, conn);
        return -1;
    }
    out->items = NULL;
    out->count = 0;
        char query[256];
        snprintf(query, sizeof(query),
                 "SELECT id, user_id, contact_user_id, alias, group_name, UNIX_TIMESTAMP(created_at) "
             "FROM contacts WHERE user_id=%llu ORDER BY alias",
             (unsigned long long)user_id);
    int rc = -1;
    if (mysql_query(conn, query) == 0) {
        MYSQL_RES *res = mysql_store_result(conn);
        size_t rows = mysql_num_rows(res);
    out->items = static_cast<contact_record_t *>(calloc(rows, sizeof(contact_record_t)));
        out->count = rows;
        size_t idx = 0;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != NULL) {
            fill_contact_row(row, &out->items[idx++]);
        }
        mysql_free_result(res);
        rc = 0;
    } else {
        LOGE("mysql: list_contacts failed: %s", mysql_error(conn));
    }
    release_conn(db, conn);
    return rc;
}

int db_add_contact(db_handle_t *db, uint64_t user_id, const char *alias, const char *group_name, uint64_t contact_user_id, contact_record_t *out) {
    MYSQL *conn = acquire_conn(db);
    if (!conn) return -1;
    char *esc_alias = NULL;
    char *esc_group = NULL;
    escape_dup(conn, alias, &esc_alias);
    escape_dup(conn, group_name, &esc_group);
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO contacts (user_id, contact_user_id, alias, group_name) VALUES (%llu, %llu, '%s', '%s')",
             (unsigned long long)user_id,
             (unsigned long long)contact_user_id,
             esc_alias ? esc_alias : "",
             esc_group ? esc_group : "");
    int rc = -1;
    if (mysql_query(conn, query) == 0) {
        if (out) {
            out->id = mysql_insert_id64(conn);
            out->user_id = user_id;
            out->contact_user_id = contact_user_id;
            strncpy(out->alias, alias, sizeof(out->alias) - 1);
            strncpy(out->group_name, group_name ? group_name : "", sizeof(out->group_name) - 1);
            out->created_at = time(NULL);
        }
        rc = 0;
    } else {
        LOGE("mysql: add_contact failed: %s", mysql_error(conn));
    }
    free(esc_alias);
    free(esc_group);
    release_conn(db, conn);
    return rc;
}

#endif
