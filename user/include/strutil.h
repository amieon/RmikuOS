#ifndef _STRUTIL_H
#define _STRUTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"
#include "string.h"

static inline void trim(char *str) {
    if (str == NULL) return;

    size_t len = strlen(str);
    if (len == 0) return;

    size_t start = 0;
    while (start < len && str[start] == ' ')
        start++;

    if (start == len) {
        str[0] = '\0';
        return;
    }

    size_t end = len - 1;
    while (end > 0 && str[end] == ' ')
        end--;

    size_t new_len = end - start + 1;
    for (size_t i = 0; i < new_len; i++)
        str[i] = str[start + i];
    str[new_len] = '\0';
}

static inline void trim2(char *str) {
    if (str == NULL) return;

    size_t len = strlen(str);
    if (len == 0) return;

    size_t start = 0;
    while (start < len && (str[start] == ' ' || str[start] == '>' || str[start] == '<'))
        start++;

    if (start == len) {
        str[0] = '\0';
        return;
    }

    size_t end = len - 1;
    while (end > 0 && (str[end] == ' ' || str[end] == '>' || str[end] == '<'))
        end--;

    size_t new_len = end - start + 1;
    for (size_t i = 0; i < new_len; i++)
        str[i] = str[start + i];
    str[new_len] = '\0';
}

static inline void copy_str(char *dst, const char *src, isize len) {
    int i = 0;
    for (; i < len - 1 && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* _STRUTIL_H */