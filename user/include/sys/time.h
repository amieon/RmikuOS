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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * struct itimerval 按 POSIX 归在 <sys/time.h>。
 * 本内核没有"到点发信号"的能力，所以只给结构体、不给 setitimer 原型：
 * git 用 NO_SETITIMER 自带一个 return 0 的桩函数，那个桩需要这个结构体才编得过
 * （否则会报 "struct itimerval declared inside parameter list"）。
 */
struct itimerval {
    struct timeval it_interval;   /* 周期 */
    struct timeval it_value;      /* 首次到期 */
};

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_SYS_TIME_H */
