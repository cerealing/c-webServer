# Part 1 - 多线程HTTP Web服务器

一个用C语言实现的轻量级、多线程HTTP Web服务器。

## 功能特性

- ✅ **多线程处理** - 使用pthread支持并发连接
- ✅ **HTTP/1.0协议** - 实现基本的HTTP协议
- ✅ **静态文件服务** - 支持HTML、CSS、JS、图片等文件类型
- ✅ **MIME类型识别** - 自动识别文件类型并设置正确的Content-Type
- ✅ **错误处理** - 400、404、405错误页面
- ✅ **路径安全** - 防止目录穿越攻击
- ✅ **Socket重用** - SO_REUSEADDR选项，便于调试

## 文件说明

- `myweb.c` - 主要的Web服务器源代码
- `myweb` - 编译后的可执行文件
- `learn.html` - 测试用的HTML文件
- `READE.md` - 原始的说明文件

## 编译和运行

```bash
# 编译
gcc -Wall -Wextra -pthread myweb.c -o myweb

# 运行（在8080端口）
./myweb 8080

# 测试
curl http://localhost:8080/learn.html
# 或在浏览器中访问 http://localhost:8080
```

## 技术特点

### 1. 模块化设计
- `send_headers()` - 统一的HTTP头部发送
- `send_body_mem()` - 内存响应体发送
- `send_file()` - 流式文件发送
- `guess_mime()` - MIME类型检测

### 2. 内存管理
- 使用`fdopen()`将socket包装为FILE*流
- 正确的文件描述符复制和关闭
- 线程安全的内存分配

### 3. 安全特性
- URL路径清理和验证
- 防止目录穿越攻击
- 输入长度限制

## 支持的文件类型

| 扩展名 | MIME类型 |
|--------|----------|
| .html, .htm | text/html; charset=utf-8 |
| .css | text/css; charset=utf-8 |
| .js | application/javascript; charset=utf-8 |
| .json | application/json; charset=utf-8 |
| .png | image/png |
| .jpg, .jpeg | image/jpeg |
| .gif | image/gif |
| .svg | image/svg+xml |
| .pdf | application/pdf |

## 性能特点

- 使用8KB缓冲区进行高效的文件传输
- pthread_detach确保线程资源自动回收
- 流式文件传输，支持大文件

## 代码亮点

```c
// 优雅的HTTP头部发送
static void send_headers(FILE* fp, const char* status_line,
                         const char* content_type, size_t content_length,
                         const char* extra_headers);

// 高效的文件传输
while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    fwrite(buf, 1, n, fp);
}
```

这是一个学习C语言网络编程和HTTP协议的优秀示例项目！