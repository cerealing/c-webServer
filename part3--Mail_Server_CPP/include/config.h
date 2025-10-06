#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

typedef enum {
    DB_BACKEND_STUB,
    DB_BACKEND_MYSQL
} db_backend_kind;

typedef struct {
    char listen_address[64];
    int port;
    int max_connections;
    int thread_pool_size;
    char static_dir[256];
    char template_dir[256];
    char data_dir[256];
    char log_path[256];
    db_backend_kind backend;

    struct {
        char host[128];
        int port;
        char user[64];
        char password[128];
        char database[64];
        int pool_size;
    } mysql;

    char session_secret[128];
} server_config;

int config_load(const char *path, server_config *cfg);
void config_set_defaults(server_config *cfg);

#endif // CONFIG_H
