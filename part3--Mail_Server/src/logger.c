#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static FILE *log_fp = NULL;
static log_level_t min_level = LOG_INFO;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_str(log_level_t lvl) {
    switch (lvl) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        case LOG_FATAL: return "FATAL";
        default:        return "UNK";
    }
}

int logger_init(const char *path) {
    pthread_mutex_lock(&log_mutex);
    if (log_fp) {
        pthread_mutex_unlock(&log_mutex);
        return 0;
    }
    if (!path || strcmp(path, "-") == 0) {
        log_fp = stderr;
    } else {
        log_fp = fopen(path, "a");
        if (!log_fp) {
            pthread_mutex_unlock(&log_mutex);
            return -1;
        }
        setvbuf(log_fp, NULL, _IOLBF, 0);
    }
    pthread_mutex_unlock(&log_mutex);
    return 0;
}

void logger_set_level(log_level_t level) {
    pthread_mutex_lock(&log_mutex);
    min_level = level;
    pthread_mutex_unlock(&log_mutex);
}

void logger_log(log_level_t level, const char *fmt, ...) {
    pthread_mutex_lock(&log_mutex);
    if (!log_fp) {
        log_fp = stderr;
    }
    if (level < min_level) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    fprintf(log_fp, "%s [%s] ", ts, level_str(level));

    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_fp, fmt, ap);
    va_end(ap);

    fputc('\n', log_fp);
    pthread_mutex_unlock(&log_mutex);
}

void logger_close(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_fp && log_fp != stderr) {
        fclose(log_fp);
    }
    log_fp = NULL;
    pthread_mutex_unlock(&log_mutex);
}
