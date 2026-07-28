#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"
#include "types.h"
#include "string.h"

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

/* 设置环境变量。overwrite=0 且已存在则不覆盖。返回 0 成功，-1 失败。 */
static inline isize setenv(const char *key, usize key_len, const char *val, usize val_len, usize overwrite) {
    return syscall6(SYS_SETENV, (usize)key, key_len, (usize)val, val_len, overwrite, 0);
}

static inline isize setenv_s(const char *key, const char *val, usize overwrite) {
    return setenv(key, strlen(key), val, strlen(val), overwrite);
}

/* 删除环境变量。返回 0。 */
static inline isize unsetenv(const char *key, usize key_len) {
    return syscall3(SYS_UNSETENV, (usize)key, key_len, 0);
}

static inline isize unsetenv_s(const char *key) {
    return unsetenv(key, strlen(key));
}

/* 清空全部环境变量。返回 0。 */
static inline isize clearenv(void) {
    return syscall3(SYS_CLEARENV, 0, 0, 0);
}

/* 枚举环境变量。buf 填入 "KEY=VALUE\0..."，返回总字节数（含结尾 NUL）。
 * buf=0 时返回所需大小。 */
static inline isize listenv(char *buf, usize buf_len) {
    return syscall3(SYS_LISTENV, (usize)buf, buf_len, 0);
}

#ifdef __cplusplus
}
#endif