#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

int logger_init(const char *path);
void logger_set_level(log_level_t level);
void logger_log(log_level_t level, const char *fmt, ...);
void logger_close(void);

#define LOGD(...) logger_log(LOG_DEBUG, __VA_ARGS__)
#define LOGI(...) logger_log(LOG_INFO, __VA_ARGS__)
#define LOGW(...) logger_log(LOG_WARN, __VA_ARGS__)
#define LOGE(...) logger_log(LOG_ERROR, __VA_ARGS__)
#define LOGF(...) logger_log(LOG_FATAL, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // LOGGER_H
