# MailCenter 全栈开发百科（Wiki 风格教程）

> 本教程面向第一次接触 C++ 网络编程与现代 Web 前端的同学。我们会以百科条目的笔法，从“为什么要这样做”到“代码里怎么写”逐段解释，确保每一个步骤、每一份配置都能查得到出处。

## 阅读导航

- [教程目标与读者画像](#教程目标与读者画像)
- [准备工作速查](#准备工作速查)
- [快速部署全流程](#快速部署全流程)
- [系统架构鸟瞰](#系统架构鸟瞰)
- [后端工作原理百科](#后端工作原理百科)
- [源文件条目索引](#源文件条目索引)
- [前端百科（HTML/CSS/JavaScript）](#前端百科htmlcssjavascript)
- [数据库设计百科](#数据库设计百科)
- [常见调试场景](#常见调试场景)
- [术语对照表](#术语对照表)
- [下一步建议](#下一步建议)

## 教程目标与读者画像

- **面向初学者**：默认读者会使用命令行，但可能第一次接触 epoll、HTTP 报文、DOM 事件或 JSON。教程会在“观察-解释-代码”三步之间来回切换，帮助你建立直觉。
- **覆盖范围**：后端 C++ 代码、前端模板与脚本、数据库表结构、部署方式全部覆盖，力争一站式查阅。
- **阅读建议**：
  - 只想快速跑起来：请跳到 [快速部署全流程](#快速部署全流程)。
  - 想理解后台事件循环：先读 [系统架构鸟瞰](#系统架构鸟瞰)，再深入 [后端工作原理百科](#后端工作原理百科)。
  - 想研究登录页面和应用界面：直接访问 [前端百科](#前端百科htmlcssjavascript)。
- **配套目录**：
  - 后端源码：`src/`
  - 前端静态资源：`static/`
  - HTML 模板：`templates/`
  - SQL 与种子数据：`sql/`
  - 配置文件：`config/`

## 准备工作速查

| 项目 | 说明 |
| --- | --- |
| 操作系统 | 已验证在 Linux x86_64（如 Ubuntu 22.04）上正常运行。 |
| 编译器 | g++ ≥ 11，支持 C++20；`make` 会自动调用系统默认 g++。 |
| 依赖库 | pthread、MySQL client。若使用内置 stub 数据库，可不安装 MySQL。 |
| 源码位置 | `/home/cereal/webdir/part3--Mail_Server_CPP`（以下均以此为根目录）。 |
| 运行端口 | 默认监听 `127.0.0.1:8080`，可在 `config/dev_mysql.json` 中调整。 |
| 日志 | `logs/` 目录下生成 `server.log`、`error.log` 等文件，方便排查。 |

## 快速部署全流程

本节像安装向导一样拆解部署步骤，并解释每条命令背后的意义。

### 1. 构建可执行文件

```bash
cd /home/cereal/webdir/part3--Mail_Server_CPP
make USE_REAL_MYSQL=1
```

- `make` 会在 `build/` 目录生成 `maild` 主程序。
- 指定 `USE_REAL_MYSQL=1` 表示启用真实 MySQL 后端；若省略该参数，项目会自动链接内存版数据库 `db_stub.cpp`，适合无数据库环境做功能演示。
- 若编译器提示缺少 `mysqlclient`，请安装 `libmysqlclient-dev`（Ubuntu 系统可执行 `sudo apt install libmysqlclient-dev`）。

### 2. 配置数据库连接

打开 `config/dev_mysql.json`，它采用最简单的键值形式：

```json
{
  "listen_address": "127.0.0.1",
  "port": 8080,
  "max_connections": 512,
  "thread_pool_size": 4,
  "static_dir": "static",
  "template_dir": "templates",
  "data_dir": "data",
  "db_backend": "mysql",
  "mysql": {
    "host": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "password": "your-password",
    "database": "mail_app",
    "pool_size": 10
  },
  "session_secret": "replace-with-strong-secret",
  "log_path": "logs/server.log"
}
```

- 请将 `user`、`password`、`database` 改成你本地或远程 MySQL 的账号信息。
- `static_dir` 与 `template_dir` 用于告诉后端 HTML 与静态资源的根目录，保持默认即可。
- `listen_address` 与 `port` 决定监听的 IP 与端口；如果只想本机访问，可把地址改成 `127.0.0.1`。
- `max_connections` 与 `thread_pool_size` 控制同时在线的连接数与工作线程数量。
- `static_dir`、`template_dir`、`data_dir` 分别对应静态资源、模板和附件落盘目录。
- `db_backend` 取值 `mysql` 或 `stub`；若选择 `stub`，即使未安装 MySQL 也能体验功能。
- `session_secret` 用于加密/签名会话令牌，请在生产环境替换成随机字符串。
- `log_path` 指定日志输出位置；设为 `"-"` 可将日志打印到标准输出，便于容器环境收集。

### 3. 初始化样例数据（可选但推荐）

如果你希望直接体验完整的邮箱功能，可导入项目自带的表结构和演示账号：

```bash
cd /home/cereal/webdir/part3--Mail_Server_CPP
mysql -u root -p mail_app < sql/schema.sql
mysql -u root -p mail_app < sql/seed.sql
```

- `schema.sql` 会创建或更新所有表、外键及触发器（全部使用 `IF NOT EXISTS`，重复执行不会破坏已有数据）。
- `seed.sql` 插入演示用户、邮件、联系人，可在 UI 中直接看到效果。

### 4. 启动开发模式

```bash
./build/maild --config config/dev_mysql.json
```

- 程序会读取配置、初始化日志、线程池、数据库连接、各项服务，然后在终端前台运行。
- 如果输出中出现“listening on 127.0.0.1:8080”，表示启动成功；此时浏览器访问 `http://127.0.0.1:8080/learn.html` 可看到静态示例页，访问 `/mail` 则进入登录界面。
- 如果要切换到内存数据库演示模式，可以执行 `make run`，它会直接读取 `config/dev_stub.json` 并省略 MySQL 依赖；若希望日志输出到终端，请在配置中将 `"log_path"` 设置为 `"-"`。

### 5. 使用 systemd 守护进程部署（可选）

若需在服务器上长期运行，可创建 systemd 服务：

1. 准备文件与用户：

    ```bash
    sudo mkdir -p /opt/maild
    sudo cp -a /home/cereal/webdir/part3--Mail_Server_CPP/build/maild /opt/maild/
    sudo cp /home/cereal/webdir/part3--Mail_Server_CPP/config/dev_mysql.json /etc/maild.json
    sudo useradd --system --no-create-home --shell /usr/sbin/nologin maild
    sudo chown maild:maild /opt/maild/maild /etc/maild.json
    ```

2. 编写 `/etc/systemd/system/maild.service`：

    ```ini
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

    PrivateTmp=yes
    ProtectSystem=full
    ProtectHome=yes
    NoNewPrivileges=yes

    [Install]
    WantedBy=multi-user.target
    ```

3. 重新加载并启动：

    ```bash
    sudo systemctl daemon-reload
    sudo systemctl enable --now maild.service
    ```

4. 常用指令：

    ```bash
    sudo systemctl status maild.service
    sudo journalctl -u maild.service -f
    sudo systemctl stop maild.service
    sudo systemctl disable maild.service
    ```

5. 清理部署（如需）：

    ```bash
    sudo rm /etc/systemd/system/maild.service
    sudo rm -rf /etc/systemd/system/maild.service.d
    sudo systemctl daemon-reload
    sudo rm -rf /opt/maild
    sudo rm /etc/maild.json
    sudo userdel maild
    ```

以上步骤完成后，systemd 将在机器启动时自动拉起服务。

## 系统架构鸟瞰

MailCenter 可以想象成“浏览器 ↔ HTTP 服务器 ↔ 数据库”的三层结构，但内部组织比典型 MVC 多了并发和路由细节：

1. **浏览器发起请求**：静态资源（如 `/learn.html`）直接返回文件；动态页面（如 `/mail`）由模板引擎渲染；API 请求走 `/api/*`。
2. **`maild` 进程监听端口**：`src/server.cpp` 使用 `epoll` 监控所有 socket，并配合 `eventfd` 感知工作线程的反馈。
3. **连接对象 (`connection_t`)**：为每个客户端维护读写缓冲、HTTP 解析状态、keep-alive 标记。
4. **线程池处理业务**：`src/thread_pool.cpp` 里的固定数量线程负责执行路由逻辑、模板渲染和数据库查询。
5. **服务层与数据库层**：`services/` 封装业务流程，`db_*.cpp` 负责 MySQL 查询或内存存储。
6. **结果回写**：主线程拿到 `worker_response_t`，拼接 HTTP 报文并写回客户端，必要时重置连接继续监听下一次请求。

整套流程可总结为“事件循环接单 → 线程池办事 → 队列回传结果 → 主线程发货”。理解这一循环后，阅读源码就有了路线图。

## 后端工作原理百科

### 运行时组件蓝图

`maild` 的核心数据结构是 `ServerRuntime`（定义在 `include/server_runtime.h`）：

- **线程池 (`thread_pool_t`)**：长期存活的工作线程，负责执行 `worker_entry`。
- **响应队列 (`concurrent_queue_t`)**：工作线程将处理结果塞进队列，主线程再取出。
- **数据库指针 (`database_iface_t`)**：统一的数据库接口，既可以指向 MySQL 实现，也可以指向 stub。
- **服务对象**：
  - `auth_service_t`：登录、注册、token 验证。
  - `mail_service_t`：收发邮件、管理文件夹、联系人、附件。
  - `template_engine_t`：渲染 `templates/` 里的 HTML。
- **路由器 (`router_t`)**：负责 URL → 业务处理器 的映射。
- **连接管理**：
  - `ConnectionTable`：`fd` 到 `connection_t` 的向量映射。
  - `max_heap`：根据连接最近活跃时间决定淘汰顺序，防止资源被闲置连接占满。

### 事件循环逐帧讲解

`src/server.cpp` 里的 `server_run` 负责把所有零件拼起来：

1. **初始化监听 socket**：设置 `SO_REUSEADDR`、`TCP_NODELAY`，并切换为非阻塞模式，避免阻塞整个进程。
2. **注册 epoll 事件**：监听 socket 与 `eventfd` 都注册到 `epoll`，并启用边缘触发 (`EPOLLET`)，减少无意义的重复通知。
3. **进入主循环**：
   - `epoll_wait` 一旦返回事件数组，程序遍历每个事件：
     - 新连接 → 调用 `accept_new_connections`，把每个 `fd` 包装成 `connection_t` 并加入 `ConnectionTable`。
     - 已有连接 → 交给 `handle_connection_event`，根据状态决定读或写。
     - `eventfd` → 说明工作线程有新结果，需要调用 `handle_worker_response`。
4. **连接超时管理**：`max_heap` 中记录最后活跃时间，超出 `max_connections` 或超时的连接会被优雅地关闭。

### 线程池与任务调度

`src/thread_pool.cpp` + `src/jobs.cpp` 构成“任务生产者/消费者”模型：

- **任务封装 (`worker_task_t`)**：包含 `connection_id`、`http_request_t` 以及指向服务对象的指针。
- **投递过程**：
  1. 主线程在解析完 HTTP 请求后调用 `dispatch_to_pool`。
  2. `dispatch_to_pool` 将任务放进 `job_queue`（循环数组），并用条件变量唤醒工作线程。
- **工作线程 (`worker_entry`)**：
  - 拿到任务后调用 `router_handle_request`。
  - 根据路径可能读取静态文件、渲染模板或访问数据库。
  - 将结果封装为 `worker_response_t`，推入 `response_queue`，再向 `eventfd` 写入一个数字通知主线程。
- **销毁流程**：`thread_pool_destroy` 发送关闭信号，等待所有线程 `pthread_join`，最后释放内存资源。

### HTTP 请求的生命周期

1. **读取阶段 (`connection_handle_read`)**：
   - 利用 `buffer_fill_from_fd` 将 socket 数据读入 `read_buf`，遇到 `EAGAIN` 表示暂时没有更多数据。
2. **解析阶段 (`http_parser_execute`)**：
   - 按 `\r\n\r\n` 切割头部与主体，验证 `Content-Length`，填充 `http_request_t`。
   - 解析失败则立即构造 400 错误响应。
3. **业务处理**：
   - 解析完成后，`process_request` 将数据转交线程池，并把连接状态切换为 `CONN_STATE_PROCESSING`，防止重复处理。
4. **生成响应**：
   - 工作线程写入 `worker_response_t`，包括 `http_response_t`、body、keep-alive 标记。
5. **写回阶段**：
   - 主线程在 `handle_worker_response` 中调用 `connection_prepare_response` 把状态行、头部、body 拼装到 `write_buf`。
   - `connection_handle_write` 把缓冲写回 socket；如果客户端请求 keep-alive，连接状态会重置成 `CONN_STATE_READING` 等待下一个请求，否则走关闭流程。

### 数据库访问与迁移

- **连接池 (`db_mysql.cpp`)**：`db_init` 会创建若干 MySQL 连接，放入互斥量保护的队列里。每次查询都会 `acquire_conn` / `release_conn`。
- **三大类操作**：
  - 用户管理：`db_authenticate`、`db_create_user`、`db_get_user_by_username`。
  - 邮件往来：`db_list_messages`、`db_get_message`、`db_send_message`、`db_save_draft`。
  - 联系人及文件夹：`db_add_contact`、`db_list_contacts`、`db_create_folder`。
- **收件人拆表**：
  - 旧版本把收件人存成逗号分隔字符串；`migrate_recipients_column` 会把历史数据迁移到 `message_recipients`。
  - `insert_message_recipients` 负责把新邮件的收件人插入子表，确保每个用户名都是独立记录。
- **事务与一致性**：发送邮件时会开启事务，同时写入发件人、收件人和附件表，失败则回滚。
- **内存数据库 (`db_stub.cpp`)**：若没有 MySQL 环境，`make` 默认编译这个文件。它使用 `std::vector` 存储演示数据，互斥锁保证线程安全，并在启动时自动填充演示数据。

### 配置与辅助工具

- **配置加载 (`src/config.cpp`)**：使用 `jsmn` 解析 JSON。`load_config` 会设置默认值，并将解析后的结构体传给 `main`。
- **日志系统 (`src/logger.cpp`)**：线程安全的追加写日志，支持输出到文件或 stderr。默认等级为 `LOG_DEBUG`，可在配置里调整。
- **模板引擎 (`src/template_engine.cpp`)**：极简 `{{key}}` 替换器。渲染 `/mail`、`/app` 等 HTML 页面。
- **通用工具 (`src/util.cpp`)**：时间戳、随机数、socket flag 设置等函数，减少重复代码。
- **内存缓冲 (`src/buffer.cpp`)**：自动扩容的字节缓冲区，避免频繁 `new/delete`。

## 源文件条目索引

这一节像条目百科一样列出 `src/` 中的所有 `.cpp` 文件，说明它们的职责、常见函数以及阅读顺序建议。

### 入口与主循环

#### `src/main.cpp`
- 解析命令行参数，调用 `load_config`。
- 初始化日志、线程池、响应队列、数据库与服务对象。
- 注册信号处理器，最后调用 `mail::server_run`。
- 建议先读这个文件，理解整个程序的装配顺序。

#### `src/server.cpp`
- 事件循环核心：`server_run`、`accept_new_connections`、`handle_connection_event`、`handle_worker_response`。
- 负责 epoll 注册、连接状态机流转、响应写回。
- 包含连接淘汰逻辑和延迟关闭机制，是理解并发模型的关键。

#### `src/connection.cpp`
- `connection_init`/`free` 管理缓冲与解析器。
- `connection_handle_read`/`write` 分别处理 I/O。
- `connection_prepare_response` 拼接 HTTP 响应头，决定 keep-alive 与否。

#### `src/buffer.cpp`
- 提供 `buffer_fill_from_fd`、`buffer_flush_to_fd`、`buffer_append` 等基础 API。
- 自动扩容策略采用倍增，适合学习环形缓冲的实现方式。

### 并发与任务调度

#### `src/thread_pool.cpp`
- `thread_pool_create` 构建固定线程数的工作池。
- `worker_main` 循环等待条件变量，执行任务后回收。
- `thread_pool_submit` 在队列满时阻塞等待，体现了生产者与消费者的协作。

#### `src/concurrent_queue.cpp`
- 链表结构的线程安全队列，用于在线程之间传递 `worker_response_t`。
- `cq_push`/`cq_pop` 采用互斥锁加条件变量的方式，易懂且稳健。

#### `src/jobs.cpp`
- 封装任务与响应的构造与析构，避免内存泄漏。
- `worker_response_dispose` 在队列销毁时统一释放内存，保持代码整洁。

#### `src/max_heap.cpp`
- 自定义二叉堆，支持“根据 fd 删除”和“取出最大值”。
- 服务器将“最后活跃时间”取负值存入优先级，这个技巧值得关注。

### HTTP 与配置

#### `src/http.cpp`、`src/http_parser.cpp`
- `http_request_init/reset/free`、`http_response_init/reset/free` 管理请求/响应对象。
- `http_parser_execute` 按照 HTTP/1.1 规范解析请求行、头部和正文。
- 提供 `http_response_set_header` 等工具函数，封装常用的头部读写。

#### `src/config.cpp`
- 使用 `jsmn` token 逐个解析 JSON 键值对。
- 支持默认值、类型校验和错误提示。

#### `src/jsmn.cpp`
- 第三方轻量 JSON 解析器的实现文件，仅需了解其返回 token 列表的机制。

#### `src/logger.cpp`
- 写入日志文件的线程安全封装。
- 默认输出格式类似 `[2025-10-07 12:34:56][INFO] message`，便于排查。

#### `src/util.cpp`
- Socket 辅助函数：`util_set_nonblocking`、`util_set_cloexec`。
- 时间工具：`util_now_ms` 用于计算连接超时。
- 字符串工具：`util_strlcpy` 防止缓冲溢出。

### 路由与模板

#### `src/template_engine.cpp`
- `template_engine_create` 记录模板根目录。
- `template_engine_render` 支持 `{{key}}` 占位符替换，可视作“超轻量模板引擎”。

#### `src/router.cpp`
- 核心入口 `router_handle_request`：判断请求走静态文件、模板还是 API。
- 静态文件：`respond_with_static` 安全拼接路径并推断 MIME。
- 模板：`respond_with_template` 调用模板引擎，注入变量。
- API：`handle_api` 将 `/api/...` 路径分发到具体处理函数，如 `handle_session_login`、`handle_message_list`。
- 额外工具：`respond_with_json`、`respond_with_error`、`split_path_query`。

### 服务与数据库

#### `src/services/auth_service.cpp`
- 管理登录、注册、注销、token 验证。
- `auth_service_login` 调用 `db_authenticate`，成功后生成 64 字节随机 token。
- 内部使用固定大小数组存储 session，并定期剔除过期项。

#### `src/services/mail_service.cpp`
- 处理邮件、文件夹、联系人、附件等业务。
- `mail_service_compose` 负责解析前端传来的 Base64 附件并落盘，再调用数据库接口。
- 提供 `mail_service_list_messages`、`mail_service_get_message`、`mail_service_star` 等高层 API。

#### `src/db_mysql.cpp`
- 真实 MySQL 实现：连接池、事务、SQL 拼接都在这里。
- `insert_message_recipients` 将收件人拆分成子表行。
- `migrate_recipients_column` 保证旧库自动升级，避免手动迁移。

#### `src/db_stub.cpp`
- 内存版数据库：使用 `std::vector` 存储演示数据。
- 启动时预设 `alice`、`bob` 等用户，并调用 `stub_seed_messages_and_contacts` 生成示例邮件。
- 所有 `db_*` 函数都尽量模拟 MySQL 实现的行为，便于替换。

## 前端百科（HTML/CSS/JavaScript）

前端由两类资源组成：**静态页面**（直接从 `static/` 目录返回）与 **模板页面**（通过模板引擎渲染）。两者共同依赖 `static/css/main.css` 与 `static/js/*.js`。

### 静态演示页：`static/learn.html`

- **结构**：
  - `<!DOCTYPE html>` 声明 HTML5 标准。
  - `<head>` 内自带 `<style>`，演示如何在单页里嵌入 CSS。
  - `<main>` 包含两列：左侧文本区域、右侧图片 `figure`。
- **关键标签解释**：
  - `<meta name="viewport">` 保证移动端浏览器合适的缩放。
  - `<section>` 对内容分组，语义化比 `<div>` 更清晰。
  - `<a class="cta" href="/mail">` 提供跳转到登录页的入口。
- **CSS 看点**：
  - 使用 `clamp()` 实现字体自适应。
  - `display: grid` + `repeat(auto-fit, minmax(240px, 1fr))` 让布局在窄屏和宽屏上都自然排列。
  - `.cta:hover` 展示按钮悬停效果，帮助理解 `transition` 与 `box-shadow`。

### 模板：`templates/login.html`

- **资源引入**：通过 `<link rel="stylesheet" href="/static/css/main.css">` 共享主样式表；底部 `<script src="/static/js/login.js" defer>` 引入登录逻辑。
- **内容结构**：
  - `.login-card` 容器内，`auth-switch` 负责切换“登录/注册”两种模式。
  - 两个 `<form>` 分别绑定 `data-auth-panel="login"` 与 `"register"`，通过 `is-hidden` 类切换显示。
  - `<p id="login-message">` 用于向用户显示状态或错误信息。
- **可访问性**：
  - `role="tablist"`、`aria-selected`、`aria-hidden` 帮助屏幕阅读器识别当前面板。
  - `autocomplete="username"` / `"current-password"` 提升表单体验。

### 模板：`templates/app.html`

- **整体布局**：
  - `<header class="topbar">` 显示品牌与退出按钮。
  - `<div class="app-shell">` 内部分成侧边栏 `<aside class="sidebar">` 与主体 `<main class="workspace">`。
  - 主体又划分为“邮件列表区域 (`.message-panel`)”和“阅读区域 (`.reader-panel`)”。
- **模态框体系**：
  - 写邮件、设置归档、联系人管理三个模态框采用同一结构：`<div class="modal hidden">` + 内部 `.modal-backdrop`、`.modal-dialog`、`.modal-content`。
  - 归档模态提供 `<datalist>` 作为自动补全建议。
- **动态元素占位符**：大量 `<span id="...">` 与 `<button id="...">` 元素等待 JavaScript 绑定事件，如 `refresh-button`、`compose-button`、`toast`等。

### 样式表：`static/css/main.css`

文件超过 700 行，建议按照“设计系统 → 登录页 → 主应用 → 模态框 → 辅助组件”的顺序阅读。

1. **设计系统（CSS 变量）**：
   - `:root` 中定义了颜色、阴影、字体变量，便于全局统一风格。
   - `color-scheme: light` 提示浏览器优先使用浅色主题。
2. **通用元素**：
   - 重置 `margin`、`box-sizing`，统一按钮、表单控件的字体与过渡动画。
   - `.field` 组件化常见表单行。
3. **登录页部分 (`.login-page`、`.login-card`)**：
   - 使用 `display: grid` 与 `place-items: center` 居中整张卡片。
   - `.auth-switch__button` 利用 `border-radius: 999px` 做 pill 样式标签。
4. **应用布局 (`.app-shell`、`.sidebar`、`.workspace`)**：
   - 双栏布局通过 CSS Grid 实现 (`grid-template-columns: 260px 1fr`)。
   - `.message-panel`、`.reader-panel` 都有统一的圆角与阴影，来自 `var(--shadow-card)`。
5. **模态框与 Toast**：
   - `.modal` 默认 `display: none`，配合 JavaScript 切换 `hidden` 类。
   - `.toast` 使用 `position: fixed` + `transition` 做消息提示，JavaScript 会控制 `hidden`/`error`/`success` 类。
6. **联系人面板**：
   - `.contacts-group-list`、`.contacts-item` 等类名控制联系人侧边栏布局，配合 `app.js` 动态渲染。

阅读 CSS 建议打开浏览器开发者工具的“Elements”面板，选中对应元素观察类名与样式。

### 登录脚本：`static/js/login.js`

- **常量与存储**：
  - `TOKEN_KEY`、`USER_KEY` 定义了 `localStorage` 的键名。
  - `setMessage(text, isError)` 统一处理状态条颜色与文本。
- **网络请求函数**：
  - `login(username, password)` 与 `registerAccount(...)` 封装 `fetch` 请求，自动处理 JSON 解析与错误信息。
  - `ensureValidExistingSession()` 在页面加载完成后立即调用，检查浏览器是否已经保存 token，若已登录直接跳转 `/app`。
- **UI 交互**：
  - `setupAuthForms()` 绑定两套 `<form>`：
    - 切换模式：监听 `[data-auth-mode]` 按钮，将对应面板添加/移除 `is-hidden`，并自动聚焦第一个输入框。
    - 登录提交：验证用户名/密码是否为空，调用 `login`，保存 token 和用户信息，最后跳转。
    - 注册提交：多一步确认密码一致，再调用 `registerAccount`。
- **入口点**：
  - `document.addEventListener("DOMContentLoaded", ...)`：先尝试恢复 session，未登录时再绑定表单事件。
- **调试建议**：可在浏览器控制台输入 `localStorage.getItem("mail.token")` 查看当前 token，或在 Network 面板观察 `/api/login` 请求。

### 主应用脚本：`static/js/app.js`

这个文件是前端逻辑最核心的部分，建议按以下顺序理解：

#### 1. 常量、状态与元素缓存

- `state` 对象存放当前登录用户、文件夹、邮件列表、选中邮件、附件、联系人等。
- `elements` 用于缓存频繁访问的 DOM 节点，避免重复 `document.getElementById`。
- `cacheElements()` 在页面加载时执行，记录所有需要操作的元素引用。

#### 2. API 包装器

- `api(path, options)` 统一发起带有 `Authorization: Bearer <token>` 的请求，并处理 JSON / 文本响应。
- `handleUnauthorized()` 若服务器返回 401，则清空本地 token 并跳转回登录页。
- 其他辅助：
  - `showToast(message, type)` 显示操作结果。
  - `hydrateFromStorage()` 从 `localStorage` 恢复用户信息，若没有 token 会重定向到登录页。

#### 3. 渲染与状态同步函数

- `renderContacts()`、`renderContactsStatus()` 负责联系人侧边栏。
- `collectArchiveGroups()` 与 `updateArchiveSuggestions()` 维护归档分组候选项。
- `folderDisplayName()`、`folderKindLabel()`、`contactDisplayName()` 等纯函数，用于把枚举值转换成中文描述。
- 邮件列表、邮件详情的渲染函数（位于文件后半段）会动态创建 `<li>`、`<article>` 节点并填充内容。

#### 4. 附件与文件处理

- `addAttachmentsFromFiles(fileList)` 遍历用户选择的文件，调用 `fileToBase64` 转换成 Base64 字符串后存入 `state.composeAttachments`。
- 提供 `bytesToSize()` 将文件大小转换成人类可读格式（如 `1.2MB`）。

#### 5. 事件绑定

- 写邮件按钮 `compose-button` → 打开模态框、重置表单。
- 保存草稿 `save-draft`、发送 `compose-form` 提交 → 调用 `/api/messages` POST，传入 `saveAsDraft` 标记或附件列表。
- 刷新按钮 `refresh-button` → 重新拉取当前文件夹邮件。
- 标星/归档按钮 → 调用对应 API，更新 UI。
- 侧边栏文件夹列表 → 通过事件委托切换 `state.selectedFolder`。
- 联系人模态框 → 打开时加载联系人列表，提交表单创建新联系人。

#### 6. 页面初始化流程

1. `document.addEventListener("DOMContentLoaded", ...)`：依次调用 `cacheElements()`、`hydrateFromStorage()`。
2. 请求 `/api/mailboxes` 填充文件夹，再选择第一个文件夹加载邮件列表。
3. 绑定所有按钮与模态框关闭逻辑。
4. 显示 Toast、联系人、附件等动态反馈。

通过逐段阅读和打断点，可以清晰看到从点击按钮到调用后端 API 再到更新页面的完整链路。

## 数据库设计百科

### 表结构总览

| 表名 | 作用 | 关键字段 |
| --- | --- | --- |
| `users` | 存储账号信息 | `username`、`email`、`password_hash`、`created_at` |
| `folders` | 每位用户的文件夹（收件箱、草稿、自定义等） | `owner_id`、`kind`、`name` |
| `messages` | 邮件正文与状态 | `owner_id`、`folder`、`subject`、`body`、`is_starred`、`is_draft`、`archive_group` |
| `message_recipients` | 邮件与收件人的一对多关系 | `message_id`、`recipient_user_id`、`recipient_username` |
| `attachments` | 附件元数据与存储路径 | `message_id`、`filename`、`mime_type`、`size_bytes` |
| `contacts` | 联系人及分组 | `user_id`、`contact_user_id`、`alias`、`group_name` |

> 所有建表语句见 `sql/schema.sql`，并附带触发器 `trg_users_default_folders`，在创建新用户时自动生成默认文件夹。

### 典型业务流程

1. **登录**：`/api/login` → `auth_service_login` → `db_authenticate`（检查 `users` 表）。成功后生成 token 写入内存 session。
2. **列出文件夹**：`/api/mailboxes` → `mail_service_list_mailboxes` → `db_list_folders`（读取 `folders` 表）。
3. **收取邮件**：`/api/messages?folder=inbox` → `db_list_messages`，内部使用 `LEFT JOIN` + `GROUP_CONCAT` 聚合收件人。
4. **发送邮件**：
   - `mail_service_compose` 将附件落盘，随后调用 `db_send_message`。
   - `db_send_message` 在事务内插入发件人邮件、收件人副本、附件记录，并填充 `message_recipients`。
5. **联系人管理**：`db_add_contact` 把联系人写入 `contacts` 表，通过 `group_name` 实现分组。

### 启动时的迁移逻辑

- `db_init` 每次启动都会执行：
  - `ensure_column`、`ensure_constraint`：保证缺失的列或外键会自动补全。
  - `migrate_recipients_column`：扫描旧版 `messages.recipients` 列，若存在则拆分写入 `message_recipients` 并删除旧列。
- 借助这些迁移函数，早期版本的数据库在升级后无需手动执行脚本即可适配最新代码。

### 内存数据库（Stub）行为

- `db_stub.cpp` 使用互斥锁保护内存向量。
- 启动时预设 `alice`、`bob` 等用户，并调用 `stub_seed_messages_and_contacts` 生成示例邮件。
- 所有函数命名与 MySQL 实现完全一致，方便通过编译开关切换后端。

## 常见调试场景

- **查看实时日志**：开发模式下直接观察终端输出；systemd 部署时使用 `journalctl -u maild.service -f`。
- **检查监听端口**：`ss -tlnp | grep maild` 确认服务是否监听正确端口。
- **验证 API**：使用 `curl` 测试，例如：
  ```bash
  curl -i http://127.0.0.1:8080/api/session
  ```
  若返回 401，说明 token 缺失或失效。
- **模拟登录失败**：在 `static/js/login.js` 中找到 `setMessage`，临时插入 `console.log` 调试客户端逻辑。
- **数据库排查**：MySQL 端执行 `SHOW TABLES;`、`SELECT * FROM message_recipients LIMIT 5;` 观察数据是否写入。
- **连接数上限**：若日志出现 “too many connections”，可在配置中调大 `max_connections` 或检查客户端是否正确关闭。

## 术语对照表

| 术语 | 中文解释 | 相关文件 |
| --- | --- | --- |
| epoll | Linux 提供的高效 I/O 多路复用机制，用于同时监听多个文件描述符 | `src/server.cpp` |
| eventfd | 内核提供的事件计数器，这里用来在工作线程与主线程之间发送通知 | `src/server.cpp` |
| connection_t | 表示一个客户端连接的结构体，包含读写缓冲、解析状态和定时信息 | `src/connection.cpp` |
| worker_task_t | 线程池任务的结构体，封装 HTTP 请求与上下文 | `src/jobs.cpp` |
| keep-alive | HTTP/1.1 默认开启的连接复用机制，使同一 TCP 连接可连续处理多次请求 | `src/connection.cpp`、`src/http.cpp` |
| fetch | 浏览器提供的 JavaScript API，用于发起 HTTP 请求 | `static/js/login.js`、`static/js/app.js` |
| Base64 | 把二进制转换为 ASCII 文本的编码方式，前端附件上传使用 | `static/js/app.js` `fileToBase64`、`mail_service_compose` |

## 下一步建议

- **功能扩展**：尝试为 `/api/messages` 增加搜索或分页参数，同时在前端增加搜索框。
- **安全加固**：为登录接口加入限速、失败次数统计，或为 systemd 服务添加 `ProtectKernelLogs` 等参数。
- **前端优化**：将大段 DOM 操作拆分成组件，或引入框架（如 Vue）重写 UI，并通过路由保持后端 API 不变。
- **单元测试**：为 `db_stub.cpp` 添加更多边界测试，验证附件落盘、收件人解析等逻辑。
- **监控告警**：结合 `logger` 输出与 systemd 日志，实现简单的健康检查脚本。

愿你读完这份“百科”后，对 MailCenter 的前后端结构都了然于胸，既能部署，也能二次开发。
