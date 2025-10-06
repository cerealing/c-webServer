# Part3 Mail Server (C++ Learning Edition)

This project is the C++ port of the learning-focused mail web application. It keeps the same architecture as the C edition—`epoll`, non-blocking sockets, and a worker pool—but the runtime now compiles with `g++` (`-std=c++20`). The server still exposes an HTTP/1.1 interface (keep-alive enabled) and serves the localized single-page interface powered by server-side templates plus lightweight client-side JavaScript.

> **Why HTTP/1.1?** Implementing HTTP/2 from scratch is significantly more complex (HPACK, stream multiplexing, TLS/ALPN). For this learning project we keep the transport simple but focus on mastering high-performance I/O, connection management, and database-backed business logic. The architecture keeps room to upgrade to HTTP/2 via libraries like nghttp2.

## Highlights

- Non-blocking `epoll` main loop (no `EPOLLONESHOT`).
- Main thread handles I/O; thread pool executes heavy tasks (database, templating, attachments).
- Connection limit of 64 simultaneous keep-alive sessions; a max-heap drops the stalest connection when the limit is exceeded.
- Custom buffered reader/writer that handles `EAGAIN` gracefully.
- Pluggable database backend: real MySQL integration (via `libmysqlclient`) or an in-memory/stub fallback for development without MySQL.
- Minimal but flexible template engine powering `templates/login.html` and `templates/app.html`.
- Responsive single-page UI with modern styling (see `static/css/main.css`) that consumes REST-ish JSON APIs under `/api/*`.
- Features: login, compose internal messages (with attachments), inbox/sent/drafts/starred/archive views, custom folders, contacts list, starring/archiving toggles.

## Repository Layout

```
part3--Mail_Server_CPP/
├── config/                 # Sample configuration files
├── data/                   # Sample storage for the stub DB backend
├── include/                # Public headers
├── sql/                    # SQL schema and seed data
├── src/                    # C++ source files
├── static/                 # Front-end assets (CSS/JS/images)
├── templates/              # HTML templates consumed server-side
├── DESIGN.md               # Architecture deep dive
├── LICENSE
├── Makefile
└── README.md
```

## Build & Run

Both backends produce the same binary (`build/maild`). The only difference lies in compile flags and configuration.

### Stub backend (in-memory)

```bash
make
./build/maild --config config/dev_stub.json
```

The stub backend keeps user data inside the process and mirrors writes to `data/` so you can inspect the serialized JSON. It is ideal for hacking on the HTTP/epoll stack without installing any external dependencies.

The process starts preloaded with a realistic dataset so you can explore every mailbox view immediately. The default accounts are:

- `alice` / `alice123`
- `bob` / `bob123`
- `carol` / `carol123`
- `dave` / `dave123`
- `eve` / `eve123`

### Real MySQL backend

```bash
sudo apt install build-essential libmysqlclient-dev
make USE_REAL_MYSQL=1
./build/maild --config config/dev_mysql.json
```

> 💡 No `make`? You can still compile manually:
>
> ```bash
> mkdir -p build
> g++ -std=c++20 -fpermissive -DUSE_REAL_MYSQL -Iinclude $(mysql_config --cflags) \
>     src/*.cpp src/services/*.cpp \
>     -lpthread $(mysql_config --libs) -o build/maild
> ```
>
> If `mysql_config` is missing, replace the last line with explicit `-lmysqlclient -lz -lm -lssl -lcrypto` flags.

#### Database bootstrap

1. Start a MySQL instance and create a schema named `mail_app` (or adjust `config/dev_mysql.json`).
2. Load the schema:

	```bash
	mysql -u root -p mail_app < sql/schema.sql
	```

3. Populate the rich sample dataset so both backends stay in sync:

	```bash
	mysql -u root -p mail_app < sql/seed.sql
	```

4. Launch the server with `config/dev_mysql.json` and sign in using any of the credentials listed in the stub backend section (`alice`/`alice123`, `bob`/`bob123`, ...).

### Quick smoke test

1. Start the daemon with either configuration (stub shown below):

	```bash
	./build/maild --config config/dev_stub.json
	```

2. From another terminal, request a session token (default configs bind to port **8085**) and note the `token` field in the JSON response:

	```bash
	curl -s -X POST http://127.0.0.1:8085/api/login \
	  -H 'Content-Type: application/json' \
	  -d '{"username":"alice","password":"alice123"}'
	```

	Or register a new account and receive a token in one step:

	```bash
	curl -s -X POST http://127.0.0.1:8085/api/register \
	  -H 'Content-Type: application/json' \
	  -d '{"username":"sara","email":"sara@example.com","password":"sara123"}'
	```

3. Use that token to pull Alice's inbox (replace `<token>` with the value from step 2):

	```bash
	curl -s "http://127.0.0.1:8085/api/messages?folder=inbox" \
	  -H "Authorization: Bearer <token>"
	```

You should see JSON containing the preloaded messages (Project Kickoff, Atlas daily notes, etc.). Repeat with `folder=sent`, `folder=drafts`, or a different user credential to explore the rest of the dataset.

The runtime automatically provisions default folders (Inbox/Sent/Drafts/Starred/Archive) for every user via a trigger plus a safety check in the MySQL backend.

### Configuration knobs

Both sample config files share these keys:

| Key | Description |
| --- | --- |
| `listen_address`, `port` | Socket the HTTP server binds to. |
| `max_connections` | Soft cap on concurrent keep-alive sessions. Oldest connection is recycled once the limit is hit. |
| `thread_pool_size` | Worker threads that execute blocking database or filesystem tasks. |
| `static_dir`, `template_dir` | Roots for the static asset handler and template engine. |
| `db_backend` | Either `stub` or `mysql`. |
| `mysql.*` | Connection info + pool size when `db_backend` is `mysql`. |
| `session_secret` | Used for CSRF/nonces (future work). |
| `log_path` | Optional on-disk log file. |

Tune the paths as needed; the defaults assume the binary executes from the project root.

## Front-end experience

- `GET /` serves the static landing page `static/learn.html` with links to the mail client.
- `GET /mail` renders `templates/login.html`, the localized glassmorphism-inspired auth page with sign-in and inline account creation.
- `GET /mail/app` (and `/app` for backward compatibility) renders `templates/app.html`, which bootstraps the SPA powered by `static/js/app.js`.
- `static/css/main.css` provides responsive styling (sidebar + two-pane mail reader layout) with graceful fallbacks for tablets.
- `static/js/login.js` handles authentication flow and token persistence, while `static/js/app.js` drives folders, message lists, starring, archiving, drafts, and toast notifications.

Tokens are stored in `localStorage` (learning-friendly choice). All authenticated API calls include a `Bearer` header that the router validates via the in-memory session manager inside `auth_service.cpp`.

## Next Steps

- Extend to HTTP/2 using nghttp2 or quiche.
- Replace template engine with Mustache or ctempl.
- Harden authentication (bcrypt/scrypt password hashing, JWT sessions).
- Add integration tests and CI pipeline.
