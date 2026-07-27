#ifndef _STRING_H
#define _STRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"   

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

static inline char *strerror(int err) {
    switch (err) {
        case  2: return "No such file or directory";
        case  9: return "Bad file descriptor";
        case 12: return "Out of memory";
        case 22: return "Invalid argument";
        case 28: return "No space left on device";
        case 33: return "Argument out of domain";
        case 34: return "Result too large";
        default: return "Unknown error";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* _STRING_H */