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
参考命令（在仓库根目录执行）：

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

> 如果你刚开始学 C++，可以把下面的模块想象成一支团队：有人负责排班、有人负责搬运数据、有人替你记账，还有人对外接客。我们按职责一步步拆开。

### 线程池（`src/thread_pool.cpp`）

- **解决什么问题？** 当有很多请求同时到来时，如果给每个请求都开一个线程，系统会很快“撑爆”。线程池就像一家餐厅的固定班底，把有限数量的线程反复利用。
- **创建阶段 (`thread_pool_create`)**：读取配置里要开的线程数量，提前分配好线程对象和一个任务队列。每个线程启动后就阻塞在 `worker_main`，等待被叫号。
- **接单阶段 (`thread_pool_submit`)**：主线程把封装好的任务放入队列。如果队列满了，就先等一等（防止把内存撑爆），等空出位置再继续塞。放进去后用条件变量唤醒一个休眠的工作线程。
- **打烊阶段 (`thread_pool_destroy`)**：设置“暂停营业”标记，广播唤醒所有线程让它们优雅退出，然后逐个 `pthread_join`，确保线程都真的收工。

> 小贴士：`thread_pool_submit` 里没有复杂的锁操作，只在必要的时候加锁并快速释放，初学者可以用它来学习“最小加锁范围”的思路。

### 缓冲区与 HTTP 报文（`src/buffer.cpp`、`src/connection.cpp`）

- **为什么要有缓冲区？** 网络读取不是一次就能拿到完整的 HTTP 数据包。`byte_buffer_t` 像是一个可以自动扩容的收纳盒，帮你暂存已经读到的数据。
- **读取 (`connection_handle_read`)**：当 socket 可读时，把数据倒进缓冲区里，并记录最后一次活跃时间。达到一个完整请求后，交给 HTTP 解析器 `http_parser_execute` 做语法分析。
- **写回 (`connection_prepare_response` + `connection_handle_write`)**：生成 HTTP 响应时，先把状态行、头部、正文都放进写缓冲，再一次性写出。若客户端声明了 `keep-alive`，我们把连接的状态机重置，准备下一次请求；否则关闭连接释放资源。

### 数据库访问层（`src/db_mysql.cpp`）

- **启动 (`db_init`)**：建立与 MySQL 的连接池，执行建表脚本，并调用 `migrate_recipients_column` 自动把旧表里的收件人数据拆到新表，保证升级时不会丢数据。
- **写入 (`insert_message_with_attachments`)**：把邮件正文和附件写入各自的表，然后调用 `insert_message_recipients`。这一步会把前端传来的逗号字符串拆成单个用户名，顺便查出对应的用户 ID，一条条插入 `message_recipients`。
- **查询 (`list_messages_internal`/`db_get_message`)**：SQL 里用子查询和 `GROUP_CONCAT` 把收件人重新拼回字符串，避免上层业务改动太大，同时也兼容 MySQL 的严格分组规则。

> 观察这些函数时，可重点关注“准备语句 → 绑定参数 → 执行 → 释放”这一固定流程，基本是访问数据库的通用模板。

### 连接管理与事件驱动（`src/server.cpp`）

- **全局观**：服务器的主循环使用 `epoll` 监听多个事件源，相当于安排一个值班的门卫。它守着监听 socket、用于线程通知的 `eventfd`，以及每个客户端连接。
- **接客 (`accept_new_connections`)**：有新客户端时，把 fd 设置为非阻塞模式，加入 `ConnectionTable`（可以理解成“客人名单”），并监听它的读事件。
- **处理 (`handle_connection_event`)**：根据当前状态决定是读数据还是写数据。如果线程池已经给出响应，就调用 `handle_worker_response` 把数据塞进写缓冲。
- **释放 (`ConnectionTable::erase`)**：当连接结束或超时时，负责清理 socket、缓冲区等资源，避免内存泄漏。

> 这里的状态机 (`CONN_STATE_*`) 可以帮助初学者理解“一个连接从建立到关闭会经历哪些阶段”。

### 路由与模板渲染（`src/router.cpp`、`src/template_engine.cpp`）

- **路由器 (`router_handle_request`)**：像一个前台，根据 URL 决定去哪一层处理。如果是静态文件交给 `respond_with_static`，需要渲染 HTML 的交给 `respond_with_template`，API 请求则分派到对应的业务函数。
- **模板引擎 (`template_engine_render`)**：把 HTML 模板读进来，用字符串替换填充 `{{title}}` 等占位符，生成最终页面。这里没有花哨逻辑，便于初学者理解模板系统的核心思想。
- **前端脚本**：`static/js/login.js` 处理登录注册流程，`static/js/app.js` 负责邮箱界面的交互。可以把它们当作“页面上的机器人”，负责和后端 `/api/*` 接口交流。

### 前后端协作小剧场

1. 用户打开 `/learn.html`，浏览器直接拿到静态欢迎页。
2. 点击“前往邮件登录”后，请求 `/mail`，服务器读取模板、填充变量，再把 HTML + JS 一起发给浏览器。
3. 登录按钮被点击时，`login.js` 向 `/api/session` 发送 AJAX 请求确认账号；通过后跳转到应用页并继续请求邮件列表。
4. 每封邮件的详情由后端拼装：正文来自 `messages`，收件人来自 `message_recipients` 聚合结果。前端沿用逗号分隔字符串显示，逻辑保持不变。

> 建议初学者在浏览器的网络面板里观察这些请求，可以把“路由—服务—数据库”的往返过程看得非常清楚。

## 继续探索

- 若需要重建数据库，可在仓库根目录执行前述 `schema.sql`、`seed.sql` 命令；种子脚本已更新，所有收件人信息都会同步写入 `message_recipients`，保证与应用层逻辑一致。
- 想深入调试，可开启 `logger_set_level(LOG_DEBUG)`（默认已打开），配合 `logs/server.log` 查看 `db_send_message`、`router_handle_request` 等路径的关键信息。