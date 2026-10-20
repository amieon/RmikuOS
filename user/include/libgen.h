/*
 * libgen.h —— 路径分解（POSIX）
 * POSIX 允许这两个函数修改传入的缓冲区，所以签名是 char * 而不是 const char *。
 * 调用方若不想被改，应传 xstrdup(path) 的副本。
 */
#ifndef RK_LIBGEN_H
#define RK_LIBGEN_H

#include "string.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "a/b/" -> "b"，"/" -> "/"，"" -> "." */
static inline char *basename(char *path) {
    if (!path || !*path) return (char *)".";

    usize len = strlen(path);
    while (len > 1 && path[len - 1] == '/') path[--len] = '\0';  /* 去尾斜杠，保留根 */
    if (len == 1 && path[0] == '/') return (char *)"/";

    char *s = path + len;
    while (s > path && s[-1] != '/') s--;
    return s;
}

/* "a/b" -> "a"，"/usr" -> "/"，"usr" -> "."，"/" -> "/" */
static inline char *dirname(char *path) {
    if (!path || !*path) return (char *)".";

    usize len = strlen(path);
    while (len > 1 && path[len - 1] == '/') path[--len] = '\0';

    char *slash = (char *)0;
    for (char *p = path; *p; p++) {
        if (*p == '/') slash = p;
    }
    if (!slash) return (char *)".";              /* 没有目录部分 */

    while (slash > path && slash[-1] == '/') slash--;   /* 合并连续的 '/' */
    if (slash == path) return (char *)"/";       /* 根目录 */

    *slash = '\0';
    return path;
}

#ifdef __cplusplus
}
#endif

#endif /* RK_LIBGEN_H */
