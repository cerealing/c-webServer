```bash
cd /home/cereal/webdir/part3--Mail_Server_CPP
make USE_REAL_MYSQL=1
```

编译成功（只剩下 db_mysql.cpp 里一个已存在的“未使用参数”警告）。你可以通过以下命令启动服务：

```bash
./build/maild --config config/dev_mysql.json
```

启动前请先确认 dev_mysql.json 中的 host、user、password、database 等配置与现有 MySQL 实例一致。

数据库初始化说明
如果你已经导入过 schema.sql 和 sql/seed.sql，就不需要再次执行——脚本里都是 CREATE TABLE IF NOT EXISTS，默认文件夹触发器也已经就绪，不会重复创建。
只有在以下情况才建议重新执行：
想把库恢复到示例数据的初始状态；
拉取了新的 schema 或迁移脚本。
参考命令（在仓库根目录webdir/part3--Mail_Server_CPP执行）：

```bash
mysql -u root -p mail_app < sql/schema.sql
mysql -u root -p mail_app < sql/seed.sql
```

在 systemd 下运行（用systemd守护进程，这样就可以运行到天荒地老了）
准备运行目录并拷贝配置（推荐）：

```bash
sudo mkdir -p /opt/maild
sudo cp -a /home/cereal/webdir/part3--Mail_Server_CPP/build/maild /opt/maild/
sudo cp /home/cereal/webdir/part3--Mail_Server_CPP/config/dev_mysql.json /etc/maild.json
sudo useradd --system --no-create-home --shell /usr/sbin/nologin maild
sudo chown maild:maild /opt/maild/maild /etc/maild.json
```

创建服务文件 /etc/systemd/system/maild.service：

```bash
vim /etc/systemd/system/maild.service
```

里面填写（复制粘贴）：

```bash
[Unit]
Description=Mail web server (MySQL backend)
After=network.target mysql.service
Wants=mysql.service

[Service]
Type=simple
User=maild
Group=maild
WorkingDirectory=/opt/maild
ExecStart=/opt/maild/maild --config /etc/maild.json
Restart=on-failure
RestartSec=3
LimitNOFILE=65536

# 可选的安全加固
PrivateTmp=yes
ProtectSystem=full
ProtectHome=yes
NoNewPrivileges=yes

[Install]
WantedBy=multi-user.target
```

重新加载并启动：

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now maild.service
```

```bash
sudo systemctl status maild.service
journalctl -u maild.service -f
```

停止当前运行的服务

```bash
sudo systemctl stop maild.service
```

运行后可以用下面的命令确认状态已经是 inactive (dead)：

```bash
sudo systemctl status maild.service
```

禁用开机自启
如果只想让服务在下次重启时不再自动启动，但保留 service 文件日后随时启用：

```bash
sudo systemctl disable maild.service
```

同样，默认的系统目标会不再包含它，确认命令：

```bash
systemctl is-enabled maild.service
```

应返回 disabled。

可选：彻底移除服务文件
删除 service 文件（如果之前按照 /etc/systemd/system/maild.service 放置）：

```bash
sudo rm /etc/systemd/system/maild.service
```

如果目录下有自定义的 override.conf，也一并删除：

```bash
sudo rm -rf /etc/systemd/system/maild.service.d
```

sudo systemctl daemon-reload

```bash
sudo systemctl daemon-reload
```

若将可执行文件和配置复制到了 /opt/maild、/etc/maild.json 等位置，也可以根据需要手动清理：

```bash
sudo rm -rf /opt/maild
sudo rm /etc/maild.json
sudo userdel maild   # 仅当你确定不再需要 maild 这个系统用户
```

完成以上步骤后，systemd 将不再启动或管理该服务。需要再次启用时，重新放好 service 文件并执行 sudo systemctl enable --now maild.service 即可。

教程写在这下面......

## 程序流程概览

1. **启动阶段**：`src/main.cpp` 中的 `main` 会读取 `config/dev_mysql.json`（可通过参数覆盖），随后依次初始化日志（`logger_init`）、线程池（`thread_pool_create`）、响应队列（`cq_init`）以及 MySQL 后端（`db_init`）。完成后再创建认证、邮件、模板三大服务对象，最终调用 `mail::server_run` 进入事件循环。
2. **事件循环**：`src/server.cpp` 的 `server_run` 负责监听端口、注册 `epoll` 事件以及 `eventfd`。当有新连接时由 `accept_new_connections` 创建 `connection_t`，并放入自定义的 `ConnectionTable`；数据可读则通过 `connection_handle_read` 读取到 `byte_buffer_t`，交给 `http_parser_execute` 解析；解析完成后封装为 `worker_task_t` 投递到线程池。
3. **工作线程**：线程池中的 `worker_entry` 拉起 `router_handle_request`，路由到静态文件、模板页面或 `/api/*` 接口。生成的 `http_response_t` 被推入 `response_queue`，主线程收到 `eventfd` 通知后由 `handle_worker_response` 写回客户端。
4. **连接生命周期**：写缓冲通过 `connection_prepare_response` 组织好 HTTP 报文，再由 `connection_handle_write` 刷出；若 `keep-alive` 仍成立则重置解析器，继续监听新的请求，否则进入 `CONN_STATE_CLOSING`，由 `ConnectionTable::erase` 清理资源。

## 数据库关系模式 R（满足第三范式）

> 所有表定义可参见 `sql/schema.sql`，示例数据见 `sql/seed.sql`。

- **R_users**(`id`, `username`, `email`, `password_hash`, `created_at`)
- **R_folders**(`id`, `owner_id` → R_users, `kind`, `name`, `created_at`)
- **R_messages**(`id`, `owner_id` → R_users, `folder`, `custom_folder`, `archive_group`, `subject`, `body`, `is_starred`, `is_draft`, `is_archived`, `created_at`, `updated_at`)
- **R_message_recipients**(`id`, `message_id` → R_messages, `recipient_user_id` → R_users 可为空, `recipient_username`, `created_at`)
- **R_attachments**(`id`, `message_id` → R_messages, `filename`, `storage_path`, `relative_path`, `mime_type`, `size_bytes`, `created_at`)
- **R_contacts**(`id`, `user_id` → R_users, `contact_user_id` → R_users, `alias`, `group_name`, `created_at`)

`message_recipients` 将原本的逗号分隔字符串拆成一对多关系，消除了非第一范式的问题，字段完全依赖主键，且没有跨表传递依赖——整体满足第三范式。`db_mysql.cpp` 在 `insert_message_recipients` 中负责同步写入，同时 `migrate_recipients_column` 会在启动时检查旧库并自动迁移、删除遗留的 `messages.recipients` 列。

> **提示**：如果你的数据库已经跑过旧版本，不需要删除或重建整库。启动服务或执行 `sql/schema.sql` 时会自动补齐缺失的表、列和约束；`migrate_recipients_column` 还会把旧的 `messages.recipients` 数据拆分到 `message_recipients` 表里。只有在想重置为演示数据时，才需要手动清空或重新导入 `sql/seed.sql`。

## ER 结构要点（文字版）

- **用户 (User)** 与 **文件夹 (Folder)** 一对多；系统触发器 `trg_users_default_folders` 在创建用户时自动填充默认文件夹。
- **消息 (Message)** 与 **收件人 (MessageRecipient)** 一对多；消息通过 `owner_id` 归属到用户，通过 `folder`/`custom_folder` 定位在逻辑目录。
- **消息** 与 **附件 (Attachment)** 一对多，附件记录实际存储路径与 MIME。
- **联系人 (Contact)** 在用户之间建立有向关联，并附带别名、分组。

## 核心模块详解

> 如果你刚开始学 C++，可以把下面的模块想象成一支团队：有人负责排班、有人负责搬货、有人记账，还有人值班看门。我们不仅告诉你他们各自做什么，还把关键函数一层层拆开，帮你看懂源码里的每一步。

### 线程池（`src/thread_pool.cpp`）

- **整体目标**：`thread_pool_t` 保持一批长期存活的工作线程，用循环队列存储待办任务，避免频繁创建/销毁线程。
- **构造 (`thread_pool_create`)**：
	1. 读取配置，分配 `thread_pool` 结构体和 `pthread_t` 数组。
	2. 调用 `job_queue_init` 初始化环形缓冲，设置头尾索引。
	3. 初始化互斥量/条件变量；逐个 `pthread_create` 出工作线程，线程入口是 `worker_main`。
- **工作循环 (`worker_main`)**：
	- 先加锁，等待条件变量 `cond_jobs` 告知有任务；如果收到关闭信号且队列空，干脆退出线程。
	- 使用 `job_queue_pop` 取出首个任务，若队列变空则唤醒等待“队列已清空”的生产者 `cond_empty`。
	- 释放锁后执行 `job.fn(job.arg)`，这样主线程就能继续投递新任务。
- **投递 (`thread_pool_submit`)**：
	- 加锁后，如果队列已满，等待 `cond_empty`，直到有消费者把任务取走。
	- 将 `tp_job_t` 放进循环队列（`job_queue_push`），唤醒 `cond_jobs`，释放锁。
- **销毁 (`thread_pool_destroy`)**：
	- 置位 `shutting_down` 并广播唤醒所有线程。
	- `pthread_join` 等待每个线程退出，最后销毁互斥量、条件变量并释放内存。
- **配套的数据结构**：`job_queue_*` 函数负责维护循环数组，每次推送或弹出都更新 `head`、`tail`、`size`，初学者可以借此理解无锁队列与环形缓冲的基本写法。

### 缓冲区与 HTTP 报文（`src/buffer.cpp`、`src/connection.cpp`）

- **缓冲区结构 (`byte_buffer_t`)**：内部只有一块连续内存和读写指针；`buffer_init` 先分配初始容量，`buffer_reset` 和 `buffer_free` 用于复用或释放。
- **自动扩容 (`buffer_fill_from_fd` / `buffer_append`)**：
	- 读取或写入前都会检查剩余空间，空间不够就用 `std::realloc` 把容量翻倍。
	- 这样实现的优点是简单，缺点是需要关注潜在的内存拷贝成本，但应付入门项目完全够用。
- **读取路径 (`connection_handle_read`)**：
	1. 调用 `buffer_fill_from_fd` 把 socket 中的数据读入 `read_buf`。
	2. 根据返回值判断三种情形：0 表示对端关闭、负数但是 `EAGAIN`/`EWOULDBLOCK` 说明暂时没数据、其余负数说明出现硬错误需要断开连接。
	3. 更新 `last_activity_ms`；后续 `http_parser_execute` 会直接从 `read_buf` 取数据解析。
- **写入路径 (`connection_prepare_response` + `connection_handle_write`)**：
	- `connection_prepare_response` 负责把 HTTP 状态行、头部、正文拼进 `write_buf`，并根据 `res->keep_alive` 决定连接是否复用。
	- `connection_handle_write` 用 `buffer_flush_to_fd` 写出数据。若全部写完且是 keep-alive，重置解析器重回读状态；否则切换成 `CONN_STATE_CLOSING` 等待服务器收尾。
- **辅助函数**：
	- `buffer_peek`/`buffer_consume` 提供读取指针操作，可用于调试或未来改造。
	- `connection_init`/`connection_free` 负责在新连接创建或回收时初始化/释放所有成员，确保没有内存泄漏。

### 连接管理与事件驱动（`src/server.cpp`）

- **入口函数 (`server_run`)**：
	1. 创建 `ConnectionTable table{1024}`：这是一个“fd 到连接对象”的稀疏数组，后续会把指针存在 `ServerRuntime::connections` 里，方便别的模块查找。
	2. 调用 `setup_listen_socket`：新建 TCP 套接字，设置 `SO_REUSEADDR`、`TCP_NODELAY`，绑定监听地址和端口，调用 `listen`，最后通过 `util_set_nonblocking` 和 `util_set_cloexec` 让 fd 进入非阻塞+子进程不继承状态。
	3. 初始化 `epoll`：`epoll_create1` 返回的描述符保存在 `rt->epoll_fd`；随后把监听 fd 和 `eventfd` 都注册为 `EPOLLIN` 事件。
	4. 初始化连接堆 `heap_init` 和路由器 `router_init`，这两者分别负责 LRU 踢人和业务路由。
	5. 进入 `while (true)` 事件循环：`epoll_wait` 拿到触发的事件数组，再根据 fd 类型分发给对应的处理函数。
- **接入新连接 (`accept_new_connections`)**：
	1. 循环调用 `accept`，把所有已经排队的客户端一次性拿完；`sockaddr_in addr` 会被填写远端 IP 和端口。
	2. 使用 `util_set_nonblocking`/`util_set_cloexec` 让新 fd 也成为非阻塞。
	3. `auto conn_handle = make_connection(client_fd);` 创建 `connection_t` 对象，内部会调用 `connection_init` 初始化缓冲区、解析器状态。
	4. `table.insert(std::move(conn_handle));` 将连接指针放进 `ConnectionTable`，下次只要拿 fd 就能找到对应对象。
	5. `heap_push(&rt->connection_heap, {...})`：把 fd 按照“最近活跃时间越久未更新优先被踢”的规则塞进自建堆。由于堆实现是一个最大堆（见 `src/max_heap.cpp`），这里用负数优先级 `-last_activity_ms` 来反向排序——谁最久没活跃谁的优先级就越大，一旦总量超标，就会被 `heap_pop` 捞出来淘汰。
	6. 使用 `epoll_ctl` 把这个 fd 注册为 `EPOLLIN | EPOLLET`（边缘触发）的监听事件，让主循环在后续可以感知它的可读状态。
- **处理已有连接 (`handle_connection_event`)**：
	- 先从 `ConnectionTable` 拿到连接对象；如果为 `nullptr`，说明已经被清理，直接从 epoll 中移除。
	- 如果事件包含 `EPOLLHUP`/`EPOLLERR`，说明链路异常，调用 `table.erase` 清理。
	- 当状态是 `CONN_STATE_READING` 且收到 `EPOLLIN`：
		1. `connection_handle_read` 读取数据。
		2. 使用 `http_parser_execute` 解析请求；如果返回 `PARSE_COMPLETE`，调用 `process_request` 转交给线程池；若解析失败，构造 400 响应并切换到写模式。
	- 当状态是 `CONN_STATE_WRITING` 且收到 `EPOLLOUT`：调用 `connection_handle_write` 把缓冲发出去。如果写完并且保持 keep-alive，就把 epoll 事件改回 `EPOLLIN`；若要关闭则清理资源。
- **分发给工作线程 (`process_request` → `dispatch_to_pool`)**：
	- `process_request` 会把当前连接的请求结构复制到 `worker_task_t`，重置解析器，然后改状态为 `CONN_STATE_PROCESSING`，防止主线程重复消费。
	- `dispatch_to_pool` 将任务包装成 `tp_job_t` 投递给线程池，真正执行的是 `worker_entry`。
- **线程执行 (`worker_entry`)**：
	1. `router_handle_request` 根据路由表找到业务处理器（静态文件 / 模板 / API）。
	2. 构造 `worker_response_t` 存放处理结果，塞回到 `response_queue`。
	3. 调用 `notify_main`（往 `event_fd` 写入数字 1）提醒主线程有数据要回给客户端。
- **主线程回包 (`handle_worker_response`)**：
	- 先把 `event_fd` 中的计数读空，然后不断 `cq_pop` 取出 `worker_response_t`。
	- 找到对应连接，调用 `connection_prepare_response` 把响应写进写缓冲，并用 `epoll_ctl` 把连接改成观察写事件。
- **ConnectionTable 与自定义堆**：
	- `ConnectionTable` 本质是 `std::vector<std::unique_ptr<connection_t>>`，搭配自定义的 `ConnectionDeleter`，保证指针离开表时会自动调用 `connection_free` 释放缓冲区与 socket。
	- `ensure_capacity` 会按 2 的次方扩容，插入/删除都只是移动智能指针，几乎不用担心内存碎片问题。
	- `heap_push`、`heap_remove_fd`、`heap_pop` 定义在 `src/max_heap.cpp`，内部是一个支持删除的二叉最大堆。配合 `priority = -last_activity_ms` 的技巧，就能把“最久没动的连接”排到堆顶，方便快速淘汰。

### 数据库访问层（`src/db_mysql.cpp`）

- **初始化 (`db_init`)**：
	- 创建一个固定大小的 MySQL 连接池并存入 `ServerRuntime`。
	- 依次执行建表 SQL；`ensure_constraint` 会确保外键存在。
	- `migrate_recipients_column` 会巡检旧的 `messages.recipients` 列：如果发现旧列仍在，就 SELECT 出数据，写入 `message_recipients`，最后删除旧列。
- **写入流程 (`insert_message_with_attachments`)**：
	1. 先插入 `messages` 行，拿到自增 id。
	2. 遍历附件数组，调用 `insert_attachment` 写入 `attachments`。
	3. 解析逗号分隔的收件人，逐个调用 `insert_message_recipients` 写入；内部会查 `username_to_user_id`，若没找到账号就只存用户名。
- **查询流程 (`list_messages_internal` / `db_get_message`)**：
	- SQL 使用 `LEFT JOIN (SELECT ... GROUP_CONCAT)` 聚合收件人，把多行重新拼成一个字符串传给上层界面。
	- `ONLY_FULL_GROUP_BY` 兼容：SELECT 列中只包含主表字段与聚合结果，避免 MySQL 报错。
- **迁移脚本关联**：运行 `sql/schema.sql` 会创建/修改表结构，`sql/seed.sql` 则插入演示数据（也使用 `message_recipients`）。代码和脚本保持一致，方便初学者对照学习。

### 路由与模板渲染（`src/router.cpp`、`src/template_engine.cpp`）

- **路由入口 (`router_handle_request`)**：
	- 根据 `request.method` 与 `request.path` 决定调用哪类处理函数：静态文件、HTML 模板还是 `/api/*`。
	- 静态文件路径使用 `respond_with_static`，它只做文件读取和 MIME 判断；模板处理走 `respond_with_template`，把 `{{name}}` 占位符替换成实际值。
- **API 示例**：比如 `/api/messages` 会调用 `mail_service_list_messages`，后者再去查询数据库层的 `list_messages_internal`。
- **模板引擎 (`template_engine_render`)**：读取磁盘上的 HTML 文件，对简单的 `{{key}}` 占位符做字符串替换；支持基本的循环/条件，在源码里可以看到所有语法处理都是手写的。
- **前端脚本**：`static/js/login.js` 使用 `fetch` 请求 `/api/session` 和 `/api/login`；`static/js/app.js` 则轮询或点击触发请求 `/api/messages`、`/api/message/send` 等接口，把返回的 JSON 渲染成页面。

### 前后端协作小剧场

1. 用户打开 `/learn.html`，浏览器直接拿到静态欢迎页。
2. 点击“前往邮件登录”后，请求 `/mail`，服务器读取模板、填充变量，再把 HTML + JS 一起发给浏览器。
3. 登录按钮被点击时，`login.js` 向 `/api/session` 发送 AJAX 请求确认账号；通过后跳转到应用页并继续请求邮件列表。
4. 每封邮件的详情由后端拼装：正文来自 `messages`，收件人来自 `message_recipients` 聚合结果。前端沿用逗号分隔字符串显示，逻辑保持不变。

> 建议初学者在浏览器开发者工具的“网络 (Network)”面板里观察这些请求，可以直观看到“路由 → 服务 → 数据库 → 回包”的全流程。

## 继续探索

- 若需要重建数据库，可在仓库根目录执行前述 `schema.sql`、`seed.sql` 命令；种子脚本已更新，所有收件人信息都会同步写入 `message_recipients`，保证与应用层逻辑一致。
- 想深入调试，可开启 `logger_set_level(LOG_DEBUG)`（默认已打开），配合 `logs/server.log` 查看 `db_send_message`、`router_handle_request` 等路径的关键信息。

## 源文件逐一拆解

> 这一节把 `src/` 目录下的每个 `.cpp` 文件都翻译成“白话版说明书”。如果你刚接触 C/C++ 网络项目，建议找一个文件打开源码，一边对照下面的拆解。

### `src/main.cpp` —— 进程入口与总装配

- **职责**：组装配置、日志、线程池、数据库、各项服务，然后把 `ServerRuntime` 传给 `server_run`。
- **关键流程**：
	1. `main` 解析命令行参数，调用 `load_config` 得到 `ServerConfig`。
	2. `logger_init` + `logger_set_level` 打开日志；`LoggerGuard` 保证退出时自动关闭。
	3. 根据配置创建线程池（`thread_pool_create`）以及响应队列（`cq_init`）。
	4. `db_init` 选择真实 MySQL 或 stub 数据库；随后初始化认证、邮件、模板三大服务。
	5. 注册信号处理器后，调用 `mail::server_run(&runtime)` 进入真正的事件循环。

### `src/server.cpp` —— 事件驱动核心

- **职责**：监听端口、管理 epoll 事件、维护连接表、把请求送往线程池并回写响应。
- **重要函数**：
	- `server_run`：创建监听 socket、`epoll`、`eventfd`，初始化 `ConnectionTable` 和堆，开启主循环 (`epoll_wait`)。
	- `accept_new_connections`：逐个 `accept` 新连接、设置非阻塞、构造 `connection_t`、存入 `ConnectionTable` 并注册到 epoll。
	- `handle_connection_event`：根据连接状态（读/写）调用 `connection_handle_read` 或 `connection_handle_write`，完成解析、错误处理与状态机切换。
	- `process_request` / `dispatch_to_pool`：把已经解析完成的 `http_request_t` 封装成 `worker_task_t`，投递给线程池。
	- `worker_entry`：工作线程实际执行业务路由，生成 `worker_response_t` 并写入响应队列。
	- `handle_worker_response`：主线程从响应队列取结果，调用 `connection_prepare_response`，把 epoll 监听切换为写事件。
- **配套结构**：
	- `ConnectionTable`：用 `std::unique_ptr` 管理 `connection_t`，自动调用 `connection_free` 回收。
	- `max_heap`：保存最近活跃时间，超出 `max_connections` 时用 `heap_pop` 挑出最久未活跃的连接淘汰。

### `src/connection.cpp` —— 单个连接的状态机

- `connection_init` / `connection_free`：配置读写缓冲、HTTP 解析器、默认 keep-alive。
- `connection_handle_read`：调用缓冲区读 socket，并更新 `last_activity_ms`；遇到 `EAGAIN` 表示暂时没数据。
- `connection_prepare_response`：拼接 HTTP 头部、主体，写入 `write_buf`。
- `connection_handle_write`：将 `write_buf` 刷到 socket；若全部写完并保持 keep-alive，就重置解析器等待下一轮。

### `src/buffer.cpp` —— 自增缓冲区

- `buffer_init` / `buffer_free` / `buffer_reset`：开辟、释放、复用内存段。
- `buffer_fill_from_fd` / `buffer_flush_to_fd`：读写时按需 `realloc` 扩容，避免过小的缓冲造成多次系统调用。
- `buffer_append` / `buffer_peek` / `buffer_consume`：为 HTTP Header 拼装或解析提供便利的读写接口。

### `src/thread_pool.cpp` —— 任务分发后台

- `thread_pool_create`：初始化环形队列（`job_queue_*`）、互斥量、条件变量，并启动 `worker_main`。
- `worker_main`：在 `cond_jobs` 上等待任务，取出 `tp_job_t` 执行；队列清空时唤醒生产者。
- `thread_pool_submit`：阻塞等待队列有空位，推入新任务并唤醒工作线程。
- `thread_pool_destroy`：广播关闭信号、`pthread_join` 等待全部线程退出。

### `src/concurrent_queue.cpp` —— 线程安全队列

- **作用**：为主线程和工作线程之间传递 `worker_response_t`。
- 采用链表节点存储，`cq_push`/`cq_pop` 通过互斥锁保护，提供 `cq_destroy` 回收节点并释放响应内容。

### `src/max_heap.cpp` —— 自建连接堆

- 提供 `heap_push`、`heap_pop`、`heap_remove_fd` 等操作，内部用数组模拟完全二叉树。
- 服务器将“活跃时间”的负值当作优先级，这样堆顶就是最久未活跃的连接，可在超载时优先关闭。

### `src/util.cpp` —— 常用小工具

- `util_now_ms`、`util_rand64`：时间戳与伪随机数。
- `util_set_nonblocking` / `util_set_cloexec`：设置 socket 标志位。
- `util_strlcpy`：安全字符串拷贝，避免缓冲区溢出。

### `src/config.cpp` —— JSON 配置加载

- `load_config`：读取配置文件字符串，调用 `jsmn_parse` 得到 token 列表，依次解析各字段。
- 解析结果放入 `ServerConfig`，包括监听地址、线程池大小、静态资源路径以及 `mysql` 嵌套对象。
- `ServerConfig::log_target()`：辅助函数，决定日志写到文件还是 stderr。

### `src/jsmn.cpp` —— 轻量 JSON 解析器

- 第三方单文件库，负责把 JSON 切分成 token。项目仅使用“原样字符串 + 手写遍历”这一最简单的用法。

### `src/http.cpp` & `src/http_parser.cpp` —— HTTP 报文生命周期

- `http_request_init`/`reset`/`free` 与 `http_response_*`：管理请求、响应的内存与默认值。
- `http_response_set_header`、`http_header_get`：大小写不敏感地查找/写入头部。
- `http_parser_execute`：
	1. 查找 `\r\n\r\n` 分界，把请求行和 Header 切出来。
	2. 解析请求方法、路径、HTTP 版本，并保存在 `http_request_t`。
	3. 根据 `Content-Length` 决定是否继续收集请求体，直到 `PARSE_COMPLETE`。
- 解析失败会返回 `PARSE_ERROR`，服务器收到后立刻回写 400。

### `src/logger.cpp` —— 线程安全日志

- `logger_init`：打开文件或直接使用 stderr，配合互斥锁保证多线程打印不串行。
- `logger_log`：统一格式 `[时间][级别] message`。
- `logger_set_level` 控制最小日志等级，`logger_close` 在退出前收尾。

### `src/jobs.cpp` —— 任务封装

- 把 `worker_task_t`、`worker_response_t` 的构造/析构集中管理，创建时初始化 HTTP 结构，销毁时释放内存。
- `worker_response_dispose` 作为队列回调使用，确保无内存泄漏。

### `src/template_engine.cpp` —— 简易模板渲染

- `template_engine_create`：记录模板根目录。
- `template_engine_render`：逐字符扫描 HTML，遇到 `{{ key }}` 片段时用传入的 `template_var_t` 替换，可理解为最基础的“占位符替换”。
- 文件读取采用一次性读完 (`read_whole_file`)，适合轻量页面。

### `src/router.cpp` —— 统一路由与业务入口

- **总体结构**：`router_handle_request` 先解析 URL，区分静态资源、模板页面与 `/api/*`。
- **静态与模板**：
	- `respond_with_static`：根据安全路径拼接规则从 `static/` 目录读取文件，并设置对应 MIME。
	- `respond_with_template`：调用模板引擎渲染 `login.html`、`app.html` 等页面。
- **API 分流 (`handle_api`)**：
	- `/api/session`：登录 (`handle_session_login`)、校验 (`handle_session_validate`)、注销 (`handle_session_logout`)。
	- `/api/mailboxes`：调用 `mail_service_list_mailboxes` 返回文件夹列表。
	- `/api/messages`：GET 列出邮件，POST 写草稿或发送新邮件 (`handle_message_compose`)。
	- `/api/messages/{id}`：获取详情；`/star`、`/archive` 子路径分别调用 `handle_message_star`、`handle_message_archive`。
	- `/api/folders`、`/api/contacts`：新建文件夹、列出或添加联系人。
- **工具函数**：大量 `json_*` 帮助解析请求体，`respond_with_error` 统一 JSON 错误格式；`split_path_query` 把 URL 拆成路径与 Query。
- **生命周期**：`router_init`/`router_dispose` 目前没有重资源操作，但保留接口方便未来扩展。

### `src/services/auth_service.cpp` —— 会话管理

- `auth_service_create`：保存数据库句柄，初始化 session 数组与互斥锁。
- `auth_service_login`：调用 `db_authenticate` 校验密码，生成 64 字节 token 存入内存列表，并返回给前端。
- `auth_service_validate`：刷新 token 过期时间、顺便加载用户信息。
- `auth_service_logout` / `auth_service_register`：分别从 session 列表移除 token，或注册新用户后直接登录。
- 会话采用内存数组存储，`prune_expired` 定期清理过期条目。

### `src/services/mail_service.cpp` —— 邮件业务封装

- 管理对 `db_*` API 的调用，并处理附件的落盘保存。
- `mail_service_create`：准备数据目录（默认 `data/uploads`）。
- `mail_service_list_*` / `mail_service_get_message` / `mail_service_star` / `mail_service_archive` / `mail_service_create_folder` / `mail_service_list_contacts` 等函数直接转调用数据库层。
- `mail_service_compose`：
	1. 解析前端传来的 `compose_request_t`，利用 `store_attachment` 把 Base64 附件解码到磁盘。
	2. 根据 `save_as_draft` 选择调用 `db_save_draft` 还是 `db_send_message`。
- `store_attachment`：给没有文件名的附件生成唯一名字 (`util_now_ms + util_rand64`)，并填好 `attachment_record_t` 各字段。

### `src/db_mysql.cpp` —— 真·数据库实现

- **连接池**：`db_init` 创建若干 MySQL 连接，`acquire_conn`/`release_conn` 用互斥量与条件变量实现简单池。
- **建表与迁移**：初始化阶段执行 `CREATE TABLE IF NOT EXISTS`，同时调用 `ensure_column`、`ensure_constraint`、`migrate_recipients_column`，保证旧库也能升级到 3NF。
- **辅助填充**：`fill_user_row`、`fill_message_row`、`fill_attachment_row` 等把 MySQL Row 转成结构体。
- **业务函数**：
	- 用户：`db_authenticate`、`db_create_user`、`db_get_user_*`。
	- 文件夹：`db_list_folders`、`db_create_folder`，必要时执行 `ensure_default_folders`。
	- 邮件：`db_list_messages`、`db_get_message`（带收件人聚合）、`db_save_draft`、`db_send_message`（事务发送给收件人）等。
	- 附件/联系人：`db_add_contact`、`db_list_contacts` 等。
- **收件人拆表**：`insert_message_recipients` 把逗号字符串拆成多行，`username_to_user_id` 辅助补充外键；`db_send_message` 在事务里把邮件复制到收件人的收件箱。

### `src/db_stub.cpp` —— 内存版数据库

- 在未定义 `USE_REAL_MYSQL` 时编译，使用 `std::vector` 风格的动态数组模拟数据库。
- `db_init` 预置 `alice/bob/...` 等用户，并调用 `stub_seed_messages_and_contacts` 生成演示数据。
- 所有 `db_*` 函数都在互斥锁下操作内存结构，逻辑与 MySQL 版保持一致（例如 `db_send_message` 会给收件人复制一份）。
- 适合本地无 MySQL 时调试 UI/流程。

### `src/db_mysql.cpp` 与 `src/db_stub.cpp` 的选择

- 编译时定义 `USE_REAL_MYSQL`（见 `make USE_REAL_MYSQL=1`）会启用真实后端，否则使用 stub。
- 两者都实现 `db.h` 定义的同一套接口，因此其余组件无需关心差异。

### `src/concurrent_queue.cpp` + `src/jobs.cpp` + `src/thread_pool.cpp` 的协作

- 线程池取任务 → 执行 → 生成 `worker_response_t` → `cq_push` → 主线程 `handle_worker_response`。
- 建议初学者以这三个文件为线索，理解“生产者-消费者 + 线程池 + 事件驱动”的闭环。

### `src/router.cpp` 之外的前后端接口

- `static/js/login.js`、`static/js/app.js`（不在 src/ 目录）与上述 API 配合。
- 在阅读服务端源文件后，再打开这两个 JS 文件，可以完整串起“用户操作 → HTTP 请求 → 路由 → 服务 → 数据库”的流程。
