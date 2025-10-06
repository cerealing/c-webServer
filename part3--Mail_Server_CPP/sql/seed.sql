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

-- Alice authored messages (sent folder)
INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT id, 1, '', 'Project Kickoff',
	   'Team,\n\nWelcome to the Project Atlas kickoff. Please review the brief before tomorrow''s sync.\n\n- Alice',
	   'bob,carol', 0, 0, 0, NOW() - INTERVAL 4 HOUR, NOW() - INTERVAL 4 HOUR
FROM users WHERE username = 'alice';

INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT id, 1, '', 'Atlas daily notes',
	   'Stand-up summary:\n- Backend API contract finalized\n- UI polishing still pending\n\nCheers, Alice',
	   'bob,carol,dave', 0, 0, 0, NOW() - INTERVAL 3 HOUR, NOW() - INTERVAL 3 HOUR
FROM users WHERE username = 'alice';

-- Inbox copies for recipients
INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT u.id, 0, '', 'Project Kickoff',
	   'Team,\n\nWelcome to the Project Atlas kickoff. Please review the brief before tomorrow''s sync.\n\n- Alice',
	   'bob,carol', 0, 0, 0, NOW() - INTERVAL 4 HOUR, NOW() - INTERVAL 4 HOUR
FROM users u WHERE u.username IN ('bob', 'carol');

INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT u.id, 0, '', 'Atlas daily notes',
	   'Stand-up summary:\n- Backend API contract finalized\n- UI polishing still pending\n\nCheers, Alice',
	   'bob,carol,dave', 0, 0, 0, NOW() - INTERVAL 3 HOUR, NOW() - INTERVAL 3 HOUR
FROM users u WHERE u.username IN ('bob', 'carol', 'dave');

-- Bob replying to Alice
INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT id, 1, '', 'Re: Project Kickoff',
	   'Thanks Alice, attaching the revised timeline. Let''s sync at 10am.\n\n- Bob',
	   'alice', 0, 0, 0, NOW() - INTERVAL 2 HOUR, NOW() - INTERVAL 2 HOUR
FROM users WHERE username = 'bob';

INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT id, 0, '', 'Re: Project Kickoff',
	   'Thanks Alice, attaching the revised timeline. Let''s sync at 10am.\n\n- Bob',
	   'alice', 1, 0, 0, NOW() - INTERVAL 2 HOUR, NOW() - INTERVAL 2 HOUR
FROM users WHERE username = 'alice';

-- Carol sending checklist to Alice
INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT id, 1, '', 'UX Review Checklist',
	   'Hi Alice,\n\nHere is the UX review checklist for sprint 5. Please share feedback by EOD.\n\nThanks, Carol',
	   'alice', 0, 0, 0, NOW() - INTERVAL 95 MINUTE, NOW() - INTERVAL 95 MINUTE
FROM users WHERE username = 'carol';

INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT id, 0, '', 'UX Review Checklist',
	   'Hi Alice,\n\nHere is the UX review checklist for sprint 5. Please share feedback by EOD.\n\nThanks, Carol',
	   'alice', 0, 0, 0, NOW() - INTERVAL 95 MINUTE, NOW() - INTERVAL 95 MINUTE
FROM users WHERE username = 'alice';

-- Dave to Eve (archived on Eve's side)
INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT id, 1, '', 'Ops Handoff',
	   'Eve,\n\nServers patched and dashboards updated. Let me know if you see any anomalies.\n\n- Dave',
	   'eve', 0, 0, 0, NOW() - INTERVAL 80 MINUTE, NOW() - INTERVAL 80 MINUTE
FROM users WHERE username = 'dave';

INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT id, 4, '', 'Ops Handoff',
	   'Eve,\n\nServers patched and dashboards updated. Let me know if you see any anomalies.\n\n- Dave',
	   'eve', 0, 0, 1, NOW() - INTERVAL 80 MINUTE, NOW() - INTERVAL 20 MINUTE
FROM users WHERE username = 'eve';

-- Carol draft message
INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT id, 2, '', 'Content ideas',
	   'Drafting newsletter topics: AI digest, release notes, customer spotlight.',
	   'alice', 0, 1, 0, NOW() - INTERVAL 40 MINUTE, NOW() - INTERVAL 40 MINUTE
FROM users WHERE username = 'carol';

-- Custom folder message for Alice
INSERT INTO messages (owner_id, folder, custom_folder, subject, body, recipients,
					  is_starred, is_draft, is_archived, created_at, updated_at)
SELECT id, 5, 'Product', 'Weekly Product Bulletin',
	   'Feature gating complete. QA cycle starts Monday. This bulletin tracks remaining tasks.',
	   'bob,carol', 0, 0, 0, NOW() - INTERVAL 30 MINUTE, NOW() - INTERVAL 30 MINUTE
FROM users WHERE username = 'alice';

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
