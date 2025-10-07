-- Sample dataset for the learning mail server. Run after schema.sql on a fresh database.

USE mail_app;
SET NAMES utf8mb4;
SET time_zone = '+00:00';

-- Users (plaintext passwords for simplicity in the learning environment)
INSERT INTO users (username, email, password_hash, created_at)
VALUES
	('alice', 'alice@example.com', 'alice123', NOW() - INTERVAL 5 HOUR),
	('bob',   'bob@example.com',   'bob123',   NOW() - INTERVAL 4 HOUR),
	('carol', 'carol@example.com', 'carol123', NOW() - INTERVAL 3 HOUR),
	('dave',  'dave@example.com',  'dave123',  NOW() - INTERVAL 2 HOUR),
	('eve',   'eve@example.com',   'eve123',   NOW() - INTERVAL 1 HOUR);

-- Custom folder for Alice (default system folders are added via trigger)
INSERT INTO folders (owner_id, kind, name)
SELECT id, 5, 'Product' FROM users WHERE username = 'alice';

-- 缓存用户ID供后续引用
SET @alice_id = (SELECT id FROM users WHERE username = 'alice');
SET @bob_id   = (SELECT id FROM users WHERE username = 'bob');
SET @carol_id = (SELECT id FROM users WHERE username = 'carol');
SET @dave_id  = (SELECT id FROM users WHERE username = 'dave');
SET @eve_id   = (SELECT id FROM users WHERE username = 'eve');

-- Alice authored messages（发件箱副本）
INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@alice_id, 1, '', '', 'Project Kickoff',
 'Team,\n\nWelcome to the Project Atlas kickoff. Please review the brief before tomorrow''s sync.\n\n- Alice',
 0, 0, 0, NOW() - INTERVAL 4 HOUR, NOW() - INTERVAL 4 HOUR);
SET @msg_project_kickoff_sent = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES
(@msg_project_kickoff_sent, @bob_id, 'bob'),
(@msg_project_kickoff_sent, @carol_id, 'carol');

INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@alice_id, 1, '', '', 'Atlas daily notes',
 'Stand-up summary:\n- Backend API contract finalized\n- UI polishing still pending\n\nCheers, Alice',
 0, 0, 0, NOW() - INTERVAL 3 HOUR, NOW() - INTERVAL 3 HOUR);
SET @msg_atlas_notes_sent = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES
(@msg_atlas_notes_sent, @bob_id, 'bob'),
(@msg_atlas_notes_sent, @carol_id, 'carol'),
(@msg_atlas_notes_sent, @dave_id, 'dave');

-- 收件人副本（收件箱）
INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@bob_id, 0, '', '', 'Project Kickoff',
 'Team,\n\nWelcome to the Project Atlas kickoff. Please review the brief before tomorrow''s sync.\n\n- Alice',
 0, 0, 0, NOW() - INTERVAL 4 HOUR, NOW() - INTERVAL 4 HOUR);
SET @msg_project_kickoff_bob = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES
(@msg_project_kickoff_bob, @bob_id, 'bob'),
(@msg_project_kickoff_bob, @carol_id, 'carol');

INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@carol_id, 0, '', '', 'Project Kickoff',
 'Team,\n\nWelcome to the Project Atlas kickoff. Please review the brief before tomorrow''s sync.\n\n- Alice',
 0, 0, 0, NOW() - INTERVAL 4 HOUR, NOW() - INTERVAL 4 HOUR);
SET @msg_project_kickoff_carol = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES
(@msg_project_kickoff_carol, @bob_id, 'bob'),
(@msg_project_kickoff_carol, @carol_id, 'carol');

INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@bob_id, 0, '', '', 'Atlas daily notes',
 'Stand-up summary:\n- Backend API contract finalized\n- UI polishing still pending\n\nCheers, Alice',
 0, 0, 0, NOW() - INTERVAL 3 HOUR, NOW() - INTERVAL 3 HOUR);
SET @msg_atlas_notes_bob = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES
(@msg_atlas_notes_bob, @bob_id, 'bob'),
(@msg_atlas_notes_bob, @carol_id, 'carol'),
(@msg_atlas_notes_bob, @dave_id, 'dave');

INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@carol_id, 0, '', '', 'Atlas daily notes',
 'Stand-up summary:\n- Backend API contract finalized\n- UI polishing still pending\n\nCheers, Alice',
 0, 0, 0, NOW() - INTERVAL 3 HOUR, NOW() - INTERVAL 3 HOUR);
SET @msg_atlas_notes_carol = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES
(@msg_atlas_notes_carol, @bob_id, 'bob'),
(@msg_atlas_notes_carol, @carol_id, 'carol'),
(@msg_atlas_notes_carol, @dave_id, 'dave');

INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@dave_id, 0, '', '', 'Atlas daily notes',
 'Stand-up summary:\n- Backend API contract finalized\n- UI polishing still pending\n\nCheers, Alice',
 0, 0, 0, NOW() - INTERVAL 3 HOUR, NOW() - INTERVAL 3 HOUR);
SET @msg_atlas_notes_dave = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES
(@msg_atlas_notes_dave, @bob_id, 'bob'),
(@msg_atlas_notes_dave, @carol_id, 'carol'),
(@msg_atlas_notes_dave, @dave_id, 'dave');

-- Bob 回复 Alice
INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@bob_id, 1, '', '', 'Re: Project Kickoff',
 'Thanks Alice, attaching the revised timeline. Let''s sync at 10am.\n\n- Bob',
 0, 0, 0, NOW() - INTERVAL 2 HOUR, NOW() - INTERVAL 2 HOUR);
SET @msg_bob_reply_sent = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES (@msg_bob_reply_sent, @alice_id, 'alice');

INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@alice_id, 0, '', '', 'Re: Project Kickoff',
 'Thanks Alice, attaching the revised timeline. Let''s sync at 10am.\n\n- Bob',
 1, 0, 0, NOW() - INTERVAL 2 HOUR, NOW() - INTERVAL 2 HOUR);
SET @msg_bob_reply_inbox = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES (@msg_bob_reply_inbox, @alice_id, 'alice');

-- Carol 发送检查单给 Alice
INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@carol_id, 1, '', '', 'UX Review Checklist',
 'Hi Alice,\n\nHere is the UX review checklist for sprint 5. Please share feedback by EOD.\n\nThanks, Carol',
 0, 0, 0, NOW() - INTERVAL 95 MINUTE, NOW() - INTERVAL 95 MINUTE);
SET @msg_ux_sent = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES (@msg_ux_sent, @alice_id, 'alice');

INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@alice_id, 0, '', '', 'UX Review Checklist',
 'Hi Alice,\n\nHere is the UX review checklist for sprint 5. Please share feedback by EOD.\n\nThanks, Carol',
 0, 0, 0, NOW() - INTERVAL 95 MINUTE, NOW() - INTERVAL 95 MINUTE);
SET @msg_ux_inbox = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES (@msg_ux_inbox, @alice_id, 'alice');

-- Dave 发给 Eve（Eve 端已归档）
INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@dave_id, 1, '', '', 'Ops Handoff',
 'Eve,\n\nServers patched and dashboards updated. Let me know if you see any anomalies.\n\n- Dave',
 0, 0, 0, NOW() - INTERVAL 80 MINUTE, NOW() - INTERVAL 80 MINUTE);
SET @msg_ops_sent = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES (@msg_ops_sent, @eve_id, 'eve');

INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@eve_id, 4, '', '', 'Ops Handoff',
 'Eve,\n\nServers patched and dashboards updated. Let me know if you see any anomalies.\n\n- Dave',
 0, 0, 1, NOW() - INTERVAL 80 MINUTE, NOW() - INTERVAL 20 MINUTE);
SET @msg_ops_archive = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES (@msg_ops_archive, @eve_id, 'eve');

-- Carol 草稿
INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@carol_id, 2, '', '', 'Content ideas',
 'Drafting newsletter topics: AI digest, release notes, customer spotlight.',
 0, 1, 0, NOW() - INTERVAL 40 MINUTE, NOW() - INTERVAL 40 MINUTE);
SET @msg_carol_draft = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES (@msg_carol_draft, @alice_id, 'alice');

-- Alice 自定义文件夹消息
INSERT INTO messages (owner_id, folder, custom_folder, archive_group, subject, body,
                      is_starred, is_draft, is_archived, created_at, updated_at)
VALUES
(@alice_id, 5, 'Product', '', 'Weekly Product Bulletin',
 'Feature gating complete. QA cycle starts Monday. This bulletin tracks remaining tasks.',
 0, 0, 0, NOW() - INTERVAL 30 MINUTE, NOW() - INTERVAL 30 MINUTE);
SET @msg_product = LAST_INSERT_ID();
INSERT INTO message_recipients (message_id, recipient_user_id, recipient_username)
VALUES
(@msg_product, @bob_id, 'bob'),
(@msg_product, @carol_id, 'carol');

-- Contacts
INSERT INTO contacts (user_id, contact_user_id, alias)
SELECT u1.id, u2.id, 'Bob – Engineering'
FROM users u1 JOIN users u2 ON u1.username = 'alice' AND u2.username = 'bob';

INSERT INTO contacts (user_id, contact_user_id, alias)
SELECT u1.id, u2.id, 'Carol (Design)'
FROM users u1 JOIN users u2 ON u1.username = 'alice' AND u2.username = 'carol';

INSERT INTO contacts (user_id, contact_user_id, alias)
SELECT u1.id, u2.id, 'Alice PM'
FROM users u1 JOIN users u2 ON u1.username = 'bob' AND u2.username = 'alice';

INSERT INTO contacts (user_id, contact_user_id, alias)
SELECT u1.id, u2.id, 'Dave Ops'
FROM users u1 JOIN users u2 ON u1.username = 'carol' AND u2.username = 'dave';

INSERT INTO contacts (user_id, contact_user_id, alias)
SELECT u1.id, u2.id, 'Dave Support'
FROM users u1 JOIN users u2 ON u1.username = 'eve' AND u2.username = 'dave';
