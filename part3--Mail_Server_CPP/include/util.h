#ifndef UTIL_H
#define UTIL_H

#include <time.h>
#include <stdint.h>
#include <stddef.h>

long long util_now_ms(void);
int util_set_nonblocking(int fd);
int util_set_cloexec(int fd);
uint64_t util_rand64(void);
size_t util_strlcpy(char *dst, size_t dst_size, const char *src);

#endif // UTIL_H
