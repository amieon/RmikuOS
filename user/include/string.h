#ifndef _STRING_H
#define _STRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"   
#include "errno.h"   /* strerror 用符号名而不是魔法数字 */
#include "mem.h"     /* strdup 要 malloc（mem.h 自包含，不会回来引 string.h） */

/* ===== 这两个在 lib/string.c 里实现，链接时提供 ===== */
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

/* ===== 以下 static inline，头文件自包含 ===== */

static inline void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

static inline int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        x++; y++;
    }
    return 0;
}

static inline void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return NULL;
}


static inline size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

static inline size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static inline int strcoll(const char *s1, const char *s2) {
    return strcmp(s1, s2);
}

static inline int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static inline char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

static inline char *strncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n && *src) { *d++ = *src++; n--; }
    while (n--) *d++ = 0;
    return dst;
}

static inline char *strcat(char *dst, const char *src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

static inline char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst + strlen(dst);
    while (n-- && *src) *d++ = *src++;
    *d = 0;
    return dst;
}

static inline char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

static inline char *strrchr(const char *s, int c) {
    const char *last = NULL;
    do {
        if (*s == (char)c) last = s;
    } while (*s++);
    return (char *)last;
}

static inline char *strstr(const char *hay, const char *needle) {
    if (!*needle) return (char *)hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)hay;
    }
    return NULL;
}

static inline size_t strspn(const char *s, const char *accept) {
    const char *p = s;
    while (*p) {
        const char *a;
        int found = 0;
        for (a = accept; *a; a++) {
            if (*p == *a) { found = 1; break; }
        }
        if (!found) break;
        p++;
    }
    return (size_t)(p - s);
}

static inline size_t strcspn(const char *s, const char *reject) {
    const char *p = s;
    while (*p) {
        const char *r;
        for (r = reject; *r; r++) {
            if (*p == *r) return (size_t)(p - s);
        }
        p++;
    }
    return (size_t)(p - s);
}

static inline char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        const char *a;
        for (a = accept; *a; a++) {
            if (*s == *a) return (char *)s;
        }
        s++;
    }
    return NULL;
}

/* 只覆盖内核真会产生的那些（含 socket/进程），不追求与 glibc 全文一致。 */
static inline char *strerror(int err) {
    switch (err) {
        case EPERM:            return "Operation not permitted";
        case ENOENT:           return "No such file or directory";
        case ESRCH:            return "No such process";
        case EINTR:            return "Interrupted system call";
        case EIO:              return "Input/output error";
        case ENXIO:            return "No such device or address";
        case ENOEXEC:          return "Exec format error";
        case EBADF:            return "Bad file descriptor";
        case ECHILD:           return "No child processes";
        case EAGAIN:           return "Resource temporarily unavailable";
        case ENOMEM:           return "Out of memory";
        case EACCES:           return "Permission denied";
        case EFAULT:           return "Bad address";
        case EBUSY:            return "Device or resource busy";
        case EEXIST:           return "File exists";
        case EXDEV:            return "Invalid cross-device link";
        case ENODEV:           return "No such device";
        case ENOTDIR:          return "Not a directory";
        case EISDIR:           return "Is a directory";
        case EINVAL:           return "Invalid argument";
        case ENFILE:           return "Too many open files in system";
        case EMFILE:           return "Too many open files";
        case ENOTTY:           return "Inappropriate ioctl for device";
        case EFBIG:            return "File too large";
        case ENOSPC:           return "No space left on device";
        case ESPIPE:           return "Illegal seek";
        case EROFS:            return "Read-only file system";
        case EPIPE:            return "Broken pipe";
        case EDOM:             return "Argument out of domain";
        case ERANGE:           return "Result too large";
        case ENAMETOOLONG:     return "File name too long";
        case ENOSYS:           return "Function not implemented";
        case ENOTEMPTY:        return "Directory not empty";
        case ELOOP:            return "Too many levels of symbolic links";
        case ENOTSOCK:         return "Socket operation on non-socket";
        case EPROTOTYPE:       return "Protocol wrong type for socket";
        case EPROTONOSUPPORT:  return "Protocol not supported";
        case EOPNOTSUPP:       return "Operation not supported";
        case EAFNOSUPPORT:     return "Address family not supported";
        case EADDRINUSE:       return "Address already in use";
        case EADDRNOTAVAIL:    return "Cannot assign requested address";
        case ENETDOWN:         return "Network is down";
        case ENETUNREACH:      return "Network is unreachable";
        case ECONNABORTED:     return "Software caused connection abort";
        case ECONNRESET:       return "Connection reset by peer";
        case ENOBUFS:          return "No buffer space available";
        case EISCONN:          return "Transport endpoint is already connected";
        case ENOTCONN:         return "Transport endpoint is not connected";
        case ETIMEDOUT:        return "Connection timed out";
        case ECONNREFUSED:     return "Connection refused";
        case EHOSTUNREACH:     return "No route to host";
        case EALREADY:         return "Operation already in progress";
        case EINPROGRESS:      return "Operation now in progress";
        case ECANCELED:        return "Operation canceled";
        default:               return "Unknown error";
    }
}

/* ---- POSIX <strings.h> 归口 ----
 * 名字属于 strings.h，但实现只有一份，放在这里；strings.h 只做转发。
 * 比较按 unsigned char 提升（"字节语义"），与 glibc 一致：
 * 若用 char 直接相减，高位字节在默认 signed char 下会排到 ASCII 前面。 */
static inline int rk_ascii_lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static inline int strcasecmp(const char *s1, const char *s2) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    for (;;) {
        int c1 = rk_ascii_lower(*p1);
        int c2 = rk_ascii_lower(*p2);
        if (c1 != c2) return c1 - c2;
        if (c1 == 0) return 0;
        p1++; p2++;
    }
}

static inline int strncasecmp(const char *s1, const char *s2, usize n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    for (usize i = 0; i < n; i++) {
        int c1 = rk_ascii_lower(p1[i]);
        int c2 = rk_ascii_lower(p2[i]);
        if (c1 != c2) return c1 - c2;
        if (c1 == 0) return 0;
    }
    return 0;
}


static inline char *strdup(const char *s) {
    if (!s) return 0;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (!p) return 0;
    memcpy(p, s, n);
    return p;
}

#ifdef __cplusplus
}
#endif

#endif /* _STRING_H */