#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "io.h"

static inline void trim(char *str) {
    if (str == 0) return;

    usize len = strlen(str);
    if (len == 0) return;

    usize start = 0;
    while (start < len && str[start] == ' ') {
        start++;
    }

    if (start == len) {
        str[0] = '\0';
        return;
    }

    usize end = len - 1;
    while (end > 0 && str[end] == ' ') {
        end--;
    }

    usize new_len = end - start + 1;

    for (usize i = 0; i < new_len; i++) {
        str[i] = str[start + i];
    }

    str[new_len] = '\0';
}

static inline void trim2(char *str) {
    if (str == 0) return;

    usize len = strlen(str);
    if (len == 0) return;

    usize start = 0;
    while (start < len && (str[start] == ' '||str[start] == '>'||str[start] == '<')) {
        start++;
    }

    if (start == len) {
        str[0] = '\0';
        return;
    }

    usize end = len - 1;
    while (end > 0 &&  (str[end] == ' '||str[end] == '>'||str[end] == '<')) {
        end--;
    }

    usize new_len = end - start + 1;

    for (usize i = 0; i < new_len; i++) {
        str[i] = str[start + i];
    }
    str[new_len] = '\0';
}


static inline void copy_str(char *dst,const char *src, isize len){
    int i = 0;
    for(; i < len-1 && src[i] != '\0'; ++i){
        dst[i] = src[i];
    }
    dst[i] = '\0';  
}
#ifdef __cplusplus
}
#endif

// user/include/string.h （放到你 user 程序 include 路径下）
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include"types.h"

// static inline size_t strlen(const char *s) {
//     const char *p = s;
//     while (*p) p++;
//     return (size_t)(p - s);
// }

static inline size_t strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
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

#ifdef __cplusplus
}
#endif