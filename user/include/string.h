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

void trim2(char *str) {
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


void copy_str(char *dst,const char *src, isize len){
    int i = 0;
    for(; i < len-1 && src[i] != '\0'; ++i){
        dst[i] = src[i];
    }
    dst[i] = '\0';  
}
#ifdef __cplusplus
}
#endif
