#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/stat.h>

#define BUF_SIZE 8192
#define REQ_LINE_SIZE 1024
#define BACKLOG 64

static void fatal(const char* msg) {
    perror(msg);
    exit(1);
}

static const char* guess_mime(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (!strcasecmp(ext, ".html") || !strcasecmp(ext, ".htm"))   return "text/html; charset=utf-8";
    if (!strcasecmp(ext, ".txt"))                                return "text/plain; charset=utf-8";
    if (!strcasecmp(ext, ".css"))                                return "text/css; charset=utf-8";
    if (!strcasecmp(ext, ".js"))                                 return "application/javascript; charset=utf-8";
    if (!strcasecmp(ext, ".json"))                               return "application/json; charset=utf-8";
    if (!strcasecmp(ext, ".svg"))                                return "image/svg+xml";
    if (!strcasecmp(ext, ".png"))                                return "image/png";
    if (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg"))   return "image/jpeg";
    if (!strcasecmp(ext, ".gif"))                                return "image/gif";
    if (!strcasecmp(ext, ".ico"))                                return "image/x-icon";
    if (!strcasecmp(ext, ".pdf"))                                return "application/pdf";
    return "application/octet-stream";
}

static void send_headers(FILE* fp,
                         const char* status_line,
                         const char* content_type,
                         size_t content_length,
                         const char* extra_headers) {
    fprintf(fp,
            "%s\r\n"
            "Server: Mini C Web Server\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n",
            status_line, content_type, content_length);
    if (extra_headers && *extra_headers) {
        fputs(extra_headers, fp); // 每行应以 \r\n 结尾
    }
    fputs("\r\n", fp); // 结束头部
    fflush(fp);
}

static void send_body_mem(FILE* fp,
                          const char* status_line,
                          const char* content_type,
                          const void* body, size_t len,
                          const char* extra_headers) {
    send_headers(fp, status_line, content_type, len, extra_headers);
    if (len && body) fwrite(body, 1, len, fp);
    fflush(fp);
}

static void send_400(FILE* fp) {
    const char* body =
        "<html><head><meta charset=\"utf-8\"><title>400 Bad Request</title></head>"
        "<body><h1>400 请求无效</h1><p>请检查请求格式和头部。</p></body></html>";
    send_body_mem(fp, "HTTP/1.0 400 Bad Request", "text/html; charset=utf-8",
                  body, strlen(body), NULL);
}

static void send_404(FILE* fp) {
    const char* body =
        "<html><head><meta charset=\"utf-8\"><title>404 Not Found</title></head>"
        "<body><h1>404 未找到文件</h1></body></html>";
    send_body_mem(fp, "HTTP/1.0 404 Not Found", "text/html; charset=utf-8",
                  body, strlen(body), NULL);
}

static void send_405(FILE* fp) {
    const char* body =
        "<html><head><meta charset=\"utf-8\"><title>405 Method Not Allowed</title></head>"
        "<body><h1>405 不支持的请求方法</h1><p>仅支持 GET。</p></body></html>";
    send_body_mem(fp, "HTTP/1.0 405 Method Not Allowed", "text/html; charset=utf-8",
                  body, strlen(body), "Allow: GET\r\n");
}

// 文件走流式
static void send_file(FILE* fp, const char* file_path) {
    struct stat st;
    if (stat(file_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        send_404(fp);
        return;
    }
    FILE* f = fopen(file_path, "rb");
    if (!f) { send_404(fp); return; }

    const char* mime = guess_mime(file_path);
                            send_headers(fp, "HTTP/1.0 200 OK", mime, (size_t)st.st_size, NULL);

    char buf[BUF_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        fwrite(buf, 1, n, fp);
    }
    fclose(f);
    fflush(fp);
}

static void sanitize_path(char* url, char* out_path, size_t out_size) {
    // 提取路径（去掉查询串）
    char* q = strchr(url, '?');
    if (q) *q = '\0';

    // 去掉前导 '/'
    const char* p = url;
    if (*p == '/') p++;

    // 根路径 => index.html
    if (*p == '\0') {
        snprintf(out_path, out_size, "repositories.html");
        return;
    }

    // 简单防目录穿越：替换 ".."
    // 更严格可拒绝包含 ".."
    if (strstr(p, "..")) {
        snprintf(out_path, out_size, "repositories.html");
        return;
    }

    // 复制到 out_path
    snprintf(out_path, out_size, "%s", p);
}

static void* request_handler(void* arg) {
    int fd = *(int*)arg;
    free(arg);

    FILE* clnt_read = NULL;
    FILE* clnt_write = NULL;

    // 1) 建读流
    clnt_read = fdopen(fd, "r");
    if (!clnt_read) {
        close(fd);
        return NULL;
    }

    // 2) 先 dup 写 fd
    int fdw = dup(fd);
    if (fdw < 0) {
        fclose(clnt_read); // 关闭原始 fd
        return NULL;
    }

    // 3) 再用 dup 的 fd 包装写流
    clnt_write = fdopen(fdw, "w");
    if (!clnt_write) {
        close(fdw);        // 只关 dup 的这个 fd
        fclose(clnt_read); // 关原始 fd
        return NULL;
    }

    // 读取请求行
    char req_line[REQ_LINE_SIZE] = {0};
    if (!fgets(req_line, sizeof(req_line), clnt_read)) {
        send_400(clnt_write);
        fclose(clnt_read);
        fclose(clnt_write);
        return NULL;
    }

    // 基本校验
    if (strstr(req_line, "HTTP/") == NULL) {
        send_400(clnt_write);
        fclose(clnt_read);
        fclose(clnt_write);
        return NULL;
    }

    // 解析方法、URL
    char method[16] = {0};
    char url[512] = {0};
    char version[16] = {0};
    // 用空格切分
    if (sscanf(req_line, "%15s %511s %15s", method, url, version) != 3) {
        send_400(clnt_write);
        fclose(clnt_read);
        fclose(clnt_write);
        return NULL;
    }

    if (strcmp(method, "GET") != 0) {
        send_405(clnt_write);
        fclose(clnt_read);
        fclose(clnt_write);
        return NULL;
    }

    // 丢弃剩余请求头
    char line[REQ_LINE_SIZE];
    while (fgets(line, sizeof(line), clnt_read)) {
        if (strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0) break;
    }

    // 解析路径
    char path[512];
    sanitize_path(url, path, sizeof(path));

    // 发送文件
    send_file(clnt_write, path);

    fclose(clnt_read);
    fclose(clnt_write);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 2;
    }

    int serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (serv_sock < 0) fatal("socket");

    int opt = 1;
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_adr;
    memset(&serv_adr, 0, sizeof(serv_adr));
    serv_adr.sin_family = AF_INET;
    serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_adr.sin_port = htons((uint16_t)atoi(argv[1]));

    if (bind(serv_sock, (struct sockaddr*)&serv_adr, sizeof(serv_adr)) == -1)
        fatal("bind");
    if (listen(serv_sock, BACKLOG) == -1)
        fatal("listen");

    printf("Mini HTTP server running on port %s ...\n", argv[1]);

    while (1) {
        struct sockaddr_in clnt_adr;
        socklen_t clnt_adr_size = sizeof(clnt_adr);
        int s = accept(serv_sock, (struct sockaddr*)&clnt_adr, &clnt_adr_size);
        if (s < 0) {
            perror("accept");
            continue;
        }
        printf("Connection from %s:%d\n", inet_ntoa(clnt_adr.sin_addr), ntohs(clnt_adr.sin_port));

        int* ps = (int*)malloc(sizeof(int));
        if (!ps) {
            perror("malloc");
            close(s);
            continue;
        }
        *ps = s;

        pthread_t t_id;
        if (pthread_create(&t_id, NULL, request_handler, ps) != 0) {
            perror("pthread_create");
            close(s);
            free(ps);
            continue;
        }
        pthread_detach(t_id);
    }

    close(serv_sock);
    return 0;
}
