/*
 * sys/param.h —— 系统参数宏（BSD/POSIX 传统头）
 *
 * 只提供移植代码常用的几个宏。MIN/MAX 用 #ifndef 保护：
 * 有些项目自己会定义，重复定义会变成编译错误。
 */
#ifndef RK_SYS_PARAM_H
#define RK_SYS_PARAM_H

#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define NBBY 8

/* howmany(x, y): x 需要多少个 y 大小的块（向上取整） */
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define roundup(x, y) (howmany((x), (y)) * (y))
#define powerof2(x)   ((((x) - 1) & (x)) == 0)

#endif /* RK_SYS_PARAM_H */
