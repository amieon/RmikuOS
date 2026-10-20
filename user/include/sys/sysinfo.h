/*
 * sys/sysinfo.h —— 系统内存/负载信息（Linux 扩展，非 POSIX）
 *
 * 本内核没有"内存总量统计"这类全局信息（也不打算为此加 syscall），
 * 所以 sysinfo() 诚实失败：返回 -1 并置 ENOSYS。调用方按错误处理即可；
 * 结构体按 Linux/musl 的字段顺序给全，避免有人 sizeof 或取字段时炸。
 */
#ifndef RK_SYS_SYSINFO_H
#define RK_SYS_SYSINFO_H

#include "../errno.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sysinfo {
    unsigned long  uptime;                 /* 开机秒数 */
    unsigned long  loads[3];               /* 1/5/15 分钟负载 */
    unsigned long  totalram;               /* 总内存 */
    unsigned long  freeram;
    unsigned long  sharedram;
    unsigned long  bufferram;
    unsigned long  totalswap;
    unsigned long  freeswap;
    unsigned short procs;                  /* 进程数 */
    unsigned short pad;
    unsigned long  totalhigh;
    unsigned long  freehigh;
    unsigned int   mem_unit;               /* 以上各项的单位 */
    char           __reserved[256];
};

static inline int sysinfo(struct sysinfo *info) {
    (void)info;
    errno = ENOSYS;
    return -1;
}

#ifdef __cplusplus
}
#endif

#endif /* RK_SYS_SYSINFO_H */
