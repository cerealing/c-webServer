# Architecture Overview

## Goals

1. Showcase a production-inspired architecture in straight C using `epoll`, non-blocking sockets, and a worker pool.
2. Separate I/O (main thread) from heavy work (thread pool) to maintain responsive networking.
3. Provide a small, extensible business layer that interacts with MySQL (or a stub) and renders HTML templates.
4. Offer a didactic yet maintainable codebase.

## Components

### Main Event Loop (`src/server.c`)
- Creates the listening socket (`SO_REUSEADDR`, `TCP_NODELAY`, `O_NONBLOCK`).
- Registers it with `epoll` (level-triggered).
- Accepts new connections and tracks them in a `connection_table`.
- Uses a max-heap (priority queue) keyed by `last_activity` to cap live connections at 64.
- Reads incoming bytes into a per-connection buffer and parses HTTP requests incrementally.
- Writes buffered responses while handling `EAGAIN` and partial writes.
- Communicates with worker threads through lock-free queues and an `eventfd` wake-up mechanism.

### Thread Pool (`src/thread_pool.c`)
- Fixed-size pool (configurable) with work-stealing queue support.
- Accepts `task_t` structures that contain pointers to the connection context and the parsed request payload.
- Workers execute database calls, template rendering, or attachment persistence.
- Upon completion, workers push a `response_task` back to the main loop via a concurrent queue and signal the `eventfd`.

### HTTP Layer (`src/http_parser.c`, `src/http_router.c`)
- Minimal HTTP/1.1 parser that supports:
  - Request line (`method`, `path`, `version`).
  - Headers (case-insensitive lookup), chunked body not currently supported; expects `Content-Length`.
  - Persistent connections (`Connection: keep-alive`).
- Router dispatches `/api/...` JSON endpoints and static file serving for `/static/...`.
- Encodes JSON using a lightweight builder (`src/json_builder.c`).

### Buffer Management (`src/buffer.c`)
- ring buffer for reads/writes to reduce copying.
- Handles `EAGAIN` gracefully by tracking head/tail indices.

### Database Layer (`src/db_mysql.c`, `src/db_stub.c`, `include/db.h`)
- Abstract `db_backend` interface.
- MySQL implementation uses prepared statements and transactions where appropriate.
- Stub implementation mimics the interface using local JSON files; useful when MySQL libs are unavailable.

### Business Services (`src/services/*.c`)
- `auth_service.c`: login, session tokens, password hashing (`libsodium` optional fallback to PBKDF2).
- `mail_service.c`: CRUD for messages, recipients, attachments, folders, contacts.
- `attachment_service.c`: stores blobs on disk (deduplicated by SHA-256).

### Template Engine (`src/template.c`)
- Tiny Mustache-inspired engine supporting `{{var}}`, `{{#section}}`, `{{/section}}` with context dictionaries.

### Front-end (`static/`, `templates/`)
- CSS: modern, clean layout built with CSS variables and flexbox.
- JS: single-page app using vanilla ES modules (`fetch`, `FormData`, `History API`).
- Templates supply base HTML, nav, and modal components.

### Configuration (`config/*.json`)
- Defines port, thread counts, database DSN, static/template directories, stub data path, etc.

## Data Model (3NF)

- `users(id, username, email, password_hash, created_at)`
- `sessions(id, user_id, token, expires_at)`
- `folders(id, user_id, name, type)` — `type` enumerates inbox/outbox/draft/starred/archive/custom.
- `messages(id, author_id, subject, body, created_at, updated_at, archived)`
- `message_recipients(message_id, recipient_id, folder_id, is_starred, is_read)` — composite PK.
- `attachments(id, message_id, filename, storage_path, mime_type, size_bytes)`
- `message_groups(id, user_id, name)`
- `group_members(group_id, member_id)`
- `contacts(id, user_id, alias, contact_user_id)`
- `drafts(id, author_id, subject, body, updated_at)` (or reuse messages with status flag).

The schema obeys third normal form: all non-key attributes depend solely on the key, nodes, and minimal.

## Connection Limit Strategy

- Maintain a `max_heap` ordered by `last_activity_ts`.
- When exceeding 64 connection contexts, pop the most idle connection, close it, and reuse.

## Error Handling & Logging

- Central `logger` (async, thread-safe) writes to rotating files.
- Each connection carries a tracing ID for log correlation.

## Security Notes

- Passwords hashed with bcrypt (via `libbcrypt`) or fallback to PBKDF2.
- CSRF prevented via token in custom header for mutating requests.
- Sessions stored as HttpOnly cookies.

## Future Work

- TLS termination (e.g., via HAProxy or embedded wolfSSL).
- Multipart form-data parser (currently base64 JSON attachments).
- Full-text search (FTS5 or Elastic).
