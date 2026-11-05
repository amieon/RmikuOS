#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"
#include "types.h"
#include "string.h"
#include "errno.h"    /* putenv 失败要置 EINVAL */
#include "mem.h"      /* malloc/free：重建 environ 要放一张堆上的新表 */

/* crt0 把 exec 时的 envp(a2) 存进这个全局，之后再没人动过 —— 它只是启动
 * 快照。而 setenv/unsetenv/clearenv 走 syscall 只改内核里的进程环境表，
 * 两者会分叉。glibc 的 setenv 会同步维护 environ，这里得自己补上 */
extern char **environ;

/* 取环境变量到调用方提供的缓冲。
 * 返回变量值长度（不含结尾 NUL），不存在返回 -1。
 * buf=0 时仅探测所需长度。 */
static inline isize getenv_r(const char *key, usize key_len, char *buf, usize buf_len) {
    return syscall6(SYS_GETENV, (usize)key, key_len, (usize)buf, buf_len, 0, 0);
}

/* POSIX 风格：返回内部静态缓冲里的变量值指针，不存在返回 0。
 * 注意：返回的是共享静态缓冲，不可跨调用长期持有。 */
#define GETENV_BUF_SIZE 256
static char __getenv_buf[GETENV_BUF_SIZE];
static inline char *getenv(const char *key) {
    isize n = getenv_r(key, strlen(key), __getenv_buf, GETENV_BUF_SIZE - 1);
    if (n < 0) return 0;
    __getenv_buf[n] = '\0';
    return __getenv_buf;
}

/* 枚举环境变量。buf 填入 "KEY=VALUE\0..."，返回总字节数（每串各带结尾 NUL；
 * 空表返回 0）。buf=0 时仅探测所需大小。 */
static inline isize listenv(char *buf, usize buf_len) {
    return syscall3(SYS_LISTENV, (usize)buf, buf_len, 0);
}

/* 让 environ 追上内核环境表：全量重建，不做增量增删。
 *
 * 为什么全量：overwrite=0 时内核可能「成功但没改」、putenv 有裸 KEY 形式、
 * clearenv 一次清空 —— 每个入口各自判断该增/改/删哪一条，漏一个就静默
 * 分叉。拿 listenv() 的结果当唯一真相就没有这些分支。
 *
 * blob / tab 故意不释放：prep_childenv() 会把 environ 里的字符串指针存进
 * string_list 再拿去 execve，释放旧表 = 悬空指针；POSIX 也允许调用方持有
 * 环境串指针。每次 setenv 泄漏量级是环境总字节数（KB 级），进程退出即回收。
 */
static inline void rk_env_sync(void) {
    isize need = listenv(0, 0);
    if (need < 0) return;                       /* 内核出错：保持旧 environ */
    usize cap = (usize)need + 1;                /* 空表也得有一个终止 NUL */
    char *blob = (char *)malloc(cap);
    if (!blob) return;
    blob[0] = '\0';
    blob[need] = '\0';                         /* 遍历到最后一串之后要看见 0 */
    if (need > 0) {
        isize got = listenv(blob, (usize)need);
        /* 两次调用之间表长大了：内核因 buf_len 不够什么都没写，放弃这轮，
         * 别拿未初始化的 blob 去建表。下一次 setenv 会重试。 */
        if (got < 0 || got > need) { free(blob); return; }
    }
    usize n = 0;
    for (char *p = blob; *p; p += strlen(p) + 1) n++;
    char **tab = (char **)malloc((n + 1) * sizeof(char *));
    if (!tab) { free(blob); return; }
    usize i = 0;
    for (char *p = blob; *p; p += strlen(p) + 1) tab[i++] = p;
    tab[n] = 0;
    environ = tab;
}

/* 长度式底层接口。**故意不叫 setenv**：POSIX 的 setenv 是
 * (name, value, overwrite) 三参 + int 返回，名字占住就没有 POSIX 版本了。
 * 自造标识符一律加 rk_ 前缀。 */
static inline isize rk_setenv_len(const char *key, usize key_len, const char *val, usize val_len, usize overwrite) {
    isize r = syscall6(SYS_SETENV, (usize)key, key_len, (usize)val, val_len, overwrite, 0);
    if (r >= 0) rk_env_sync();
    return r;
}

static inline isize rk_unsetenv_len(const char *key, usize key_len) {
    isize r = syscall3(SYS_UNSETENV, (usize)key, key_len, 0);
    if (r >= 0) rk_env_sync();
    return r;
}

/* 便捷包装：名字/值按 strlen 算长度。返回 0 成功，负值为 -errno。 */
static inline isize setenv_s(const char *key, const char *val, usize overwrite) {
    return rk_setenv_len(key, strlen(key), val, strlen(val), overwrite);
}

static inline isize unsetenv_s(const char *key) {
    return rk_unsetenv_len(key, strlen(key));
}

/* ---- POSIX 版（<stdlib.h> 归口，env.h 被它 include） ---- */
static inline int rk_env_ret(isize r) {
    if (r < 0 && r >= -4095) { errno = (int)(-r); return -1; }
    return (int)r;
}

/* 成功返回 0，失败 -1 + errno。名字为空或含 '=' 是 EINVAL（POSIX）。 */
static inline int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || !value || strchr(name, '=')) { errno = EINVAL; return -1; }
    return rk_env_ret(rk_setenv_len(name, strlen(name), value, strlen(value), (usize)overwrite));
}

static inline int unsetenv(const char *name) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    return rk_env_ret(rk_unsetenv_len(name, strlen(name)));
}

/* 清空全部环境变量。返回 0。 */
static inline isize clearenv(void) {
    isize r = syscall3(SYS_CLEARENV, 0, 0, 0);
    if (r >= 0) rk_env_sync();
    return r;
}


static inline int putenv(char *s) {
    if (!s) { errno = EINVAL; return -1; }
    const char *eq = strchr(s, '=');
    if (!eq || eq == s) { errno = EINVAL; return -1; }
    usize nlen = (usize)(eq - s);
    char name[128];
    if (nlen >= sizeof(name)) { errno = EINVAL; return -1; }   /* 变量名过长 */
    memcpy(name, s, nlen);
    name[nlen] = '\0';
    return (int)rk_setenv_len(name, nlen, eq + 1, strlen(eq + 1), 1);
}

#ifdef __cplusplus
}
#endif