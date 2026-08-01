#ifndef RMIKU_SYS_RESOURCE_H
#define RMIKU_SYS_RESOURCE_H

/* POSIX <sys/resource.h> —— 仅 shell.c 计时用（getrusage + struct rusage）。
 * RmikuOS 无 per-process CPU 记账, 诚实返回全 0（与 SQLite 官方
 * VxWorks 兜底 #define getrusage(A,B) memset(...) 行为一致）。 */

#include "sys/types.h"
#include "time.h"       /* struct timeval */

#ifdef __cplusplus
extern "C" {
#endif

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)

struct rusage {
    struct timeval ru_utime;   /* user CPU time used */
    struct timeval ru_stime;   /* system CPU time used */
    /* 其余字段本 OS 不统计 */
};

static inline int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (!usage) return -1;
    usage->ru_utime.tv_sec = 0; usage->ru_utime.tv_usec = 0;
    usage->ru_stime.tv_sec = 0; usage->ru_stime.tv_usec = 0;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_SYS_RESOURCE_H */
