#ifndef RMIKU_SYS_TIME_H
#define RMIKU_SYS_TIME_H

/*
 * POSIX <sys/time.h> —— struct timeval / gettimeofday / usleep 都在 time.h 里实现。
 *
 * 注意必须写 "../time.h"：写成 "time.h" 时编译器先在 sys/ 目录里找，
 * 命中这个文件自己 → 守卫已定义 → 整个 include 变成空操作，
 * 结果是"包含了 sys/time.h 却看不到 struct timeval"。
 * （同一个坑之前在 sys/resource.h 踩过一次。）
 */
#include "../time.h"
#include "../io.h"      /* syscall3 / rk_sys_ret */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * struct itimerval 按 POSIX 归在 <sys/time.h>。
 *
 * 内核真的实现了 ITIMER_REAL：到点给进程挂 SIGALRM（走的是和 SIGPIPE/SIGALRM
 * 一样的 pending → trap 出口投递路径）。ITIMER_VIRTUAL / ITIMER_PROF 需要按
 * CPU 时间记账，不提供 —— 传它们会拿到 EINVAL，而不是"装上了永不响"。
 */
struct itimerval {
    struct timeval it_interval;   /* 周期 */
    struct timeval it_value;      /* 首次到期 */
};

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

/*
 * git 在没有 setitimer 时会 -DNO_SETITIMER 自带一个 return 0 的桩
 * （git-compat-util.h 里）。那种情况下这里不能再定义，否则重定义冲突。
 * 现在内核支持了， 不再设 NO_SETITIMER。
 */
#ifndef NO_SETITIMER
static inline int setitimer(int which, const struct itimerval *value, struct itimerval *old) {
    return (int)rk_sys_ret(syscall3(SYS_SETITIMER, (usize)which,
                                    (usize)value, (usize)old));
}

static inline int getitimer(int which, struct itimerval *value) {
    return (int)rk_sys_ret(syscall3(SYS_GETITIMER, (usize)which, (usize)value, 0));
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_SYS_TIME_H */
