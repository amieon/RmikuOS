/*
 * sys/utsname.h —— uname()
 *
 * 内核没有 uname syscall，也不需要：这些值是编译期确定的。
 * 字段长度 65 是 POSIX 的规定（不是随意取的）。
 */
#ifndef RK_SYS_UTSNAME_H
#define RK_SYS_UTSNAME_H

#ifdef __cplusplus
extern "C" {
#endif

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

#if defined(USER_ARCH_LOONGARCH64)
#  define RK_UTS_MACHINE "loongarch64"
#else
#  define RK_UTS_MACHINE "riscv64"
#endif

/* 内部：定长拷贝，保证 NUL 结尾 */
static inline void rk_uts_copy(char *dst, const char *src) {
    int i = 0;
    while (src[i] && i < 64) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static inline int uname(struct utsname *u) {
    if (!u) return -1;
    rk_uts_copy(u->sysname,  "RmikuOS");
    rk_uts_copy(u->nodename, "rmikuos");
    rk_uts_copy(u->release,  "0.1.0");
    rk_uts_copy(u->version,  "#1 " RK_UTS_MACHINE " RmikuOS");
    rk_uts_copy(u->machine,  RK_UTS_MACHINE);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RK_SYS_UTSNAME_H */
