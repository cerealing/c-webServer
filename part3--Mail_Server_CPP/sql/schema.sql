-- Mail Server Runtime Schema

CREATE DATABASE IF NOT EXISTS mail_app CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE mail_app;

CREATE TABLE IF NOT EXISTS users (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(64) NOT NULL UNIQUE,
    email         VARCHAR(128) NOT NULL UNIQUE,
    password_hash VARCHAR(128) NOT NULL,
    created_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS folders (
    id         BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    owner_id   BIGINT UNSIGNED NOT NULL,
    kind       INT NOT NULL,
    name       VARCHAR(64) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uniq_folder(owner_id, kind, name),
    CONSTRAINT fk_folder_owner FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS messages (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    owner_id      BIGINT UNSIGNED NOT NULL,
    folder        INT NOT NULL,
    custom_folder VARCHAR(64) NOT NULL DEFAULT '',
    archive_group VARCHAR(64) NOT NULL DEFAULT '',
    subject       VARCHAR(256) NOT NULL,
    body          MEDIUMTEXT NOT NULL,
    is_starred    TINYINT(1) NOT NULL DEFAULT 0,
    is_draft      TINYINT(1) NOT NULL DEFAULT 0,
    is_archived   TINYINT(1) NOT NULL DEFAULT 0,
    created_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at    TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_messages_owner_folder(owner_id, folder, custom_folder),
    CONSTRAINT fk_messages_owner FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS message_recipients (
    id                  BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    message_id          BIGINT UNSIGNED NOT NULL,
    recipient_user_id   BIGINT UNSIGNED NULL,
    recipient_username  VARCHAR(64) NOT NULL,
    created_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uniq_message_recipient(message_id, recipient_username),
    KEY idx_message_recipient_user(recipient_user_id),
    CONSTRAINT fk_recipient_message FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE,
    CONSTRAINT fk_recipient_user FOREIGN KEY (recipient_user_id) REFERENCES users(id) ON DELETE SET NULL
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS attachments (
    id           BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    message_id   BIGINT UNSIGNED NOT NULL,
    filename     VARCHAR(256) NOT NULL,
    storage_path VARCHAR(512) NOT NULL,
    mime_type    VARCHAR(128) NOT NULL,
    size_bytes   BIGINT UNSIGNED NOT NULL,
    created_at   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    KEY idx_attachment_message(message_id),
    CONSTRAINT fk_attachment_message FOREIGN KEY (message_id) REFERENCES messages(id) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS contacts (
    id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id         BIGINT UNSIGNED NOT NULL,
    contact_user_id BIGINT UNSIGNED NOT NULL,
    alias           VARCHAR(128) NOT NULL,
    created_at      TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uniq_contact(user_id, contact_user_id),
    CONSTRAINT fk_contact_owner FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    CONSTRAINT fk_contact_target FOREIGN KEY (contact_user_id) REFERENCES users(id) ON DELETE CASCADE
) ENGINE=InnoDB;

DROP TRIGGER IF EXISTS trg_users_default_folders;
DELIMITER $$
CREATE TRIGGER trg_users_default_folders
AFTER INSERT ON users FOR EACH ROW
BEGIN
    INSERT IGNORE INTO folders (owner_id, kind, name) VALUES
        (NEW.id, 0, 'Inbox'),
        (NEW.id, 1, 'Sent'),
        (NEW.id, 2, 'Drafts'),
        (NEW.id, 3, 'Starred'),
        (NEW.id, 4, 'Archive');
END$$
DELIMITER ;
