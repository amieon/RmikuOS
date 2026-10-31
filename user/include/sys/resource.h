#ifndef RMIKU_SYS_RESOURCE_H
#define RMIKU_SYS_RESOURCE_H

/* POSIX <sys/resource.h> —— shell.c / sqlite3 计时用（getrusage + struct rusage）。
 * RmikuOS 无 per-process CPU 记账。ru_utime 用单调钟填（不是 0）：
 * 差分才等于"这段区间流逝的墙钟时间"，填 0 会让所有测速/计时算成 0。
 * 详见 getrusage 里的说明。 */

#include "types.h"
/* 注意：这里必须是 "../time.h"。写成 "time.h" 会优先命中同目录的 sys/time.h，
 * 而那个头不提供 struct timeval —— 结果就是"单独 include 本头时 rusage 不完整"。
 * 这条是自包含检查抓出来的。 */
#include "../time.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)

/* ---- 资源限制（getrlimit）----
 * 内核没有"资源限制"这个概念，但调用方需要一组**自洽的数字**：
 * git 的 packfile.c 用 `getrlimit(RLIMIT_NOFILE)` 算出"一次能开多少 fd"，
 * 返回 -1 会让它走进"取不到上限"的分支。所以这里给真值而不是失败：
 *   RLIMIT_NOFILE = 进程 fd 上限（与 manager_wrapper.rs 的 MAX_FD_PER_PROCESS 一致）
 *   其余          = 无限制
 * getrlimit 只做"报告"，没有对应的 setrlimit —— 设置了也不会生效，
 * 与其提供一个假的 setrlimit，不如让它链接期就缺失（明确不提供）。 */
typedef unsigned long rlim_t;

struct rlimit {
    rlim_t rlim_cur;   /* soft limit */
    rlim_t rlim_max;   /* hard limit */
};

#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#define RLIMIT_NOFILE  7
#define RLIM_INFINITY  (~(rlim_t)0)

static inline int getrlimit(int resource, struct rlimit *rlim) {
    if (!rlim) return -1;
    switch (resource) {
    case RLIMIT_NOFILE:
        rlim->rlim_cur = 1024;
        rlim->rlim_max = 1024;
        break;
    case RLIMIT_STACK:
        rlim->rlim_cur = 8 * 1024 * 1024;
        rlim->rlim_max = RLIM_INFINITY;
        break;
    default:
        rlim->rlim_cur = RLIM_INFINITY;
        rlim->rlim_max = RLIM_INFINITY;
        break;
    }
    return 0;
}

struct rusage {
    struct timeval ru_utime;   /* user CPU time used */
    struct timeval ru_stime;   /* system CPU time used */
    /* 其余字段本 OS 不统计 */
};

static inline int getrusage(int who, struct rusage *usage) {
    (void)who;
    if (!usage) return -1;
    long us = (long)get_time_us();
    usage->ru_utime.tv_sec  = (time_t)(us / 1000000);
    usage->ru_utime.tv_usec = us % 1000000;
    usage->ru_stime.tv_sec  = 0;
    usage->ru_stime.tv_usec = 0;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_SYS_RESOURCE_H */
