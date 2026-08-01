#ifndef RMIKU_UTIME_H
#define RMIKU_UTIME_H

/* POSIX <utime.h> —— shell.c fileio 扩展无条件 include 它（其实从不调用
 * utime()）。RmikuOS 无文件时间戳, utime() 诚实返回 0（视为成功）。 */

#include "sys/types.h"   /* time_t */

#ifdef __cplusplus
extern "C" {
#endif

struct utimbuf {
    time_t actime;    /* access time */
    time_t modtime;   /* modification time */
};

static inline int utime(const char *path, const struct utimbuf *times) {
    (void)path; (void)times;
    return 0;   /* 本 OS 不记录时间戳 */
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_UTIME_H */
