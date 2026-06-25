#pragma once

/*
 * string.h —— 字符串工具层。
 *
 * 内容:
 *   - trim(原地去除首尾空格)
 *   - copy_str(限长拷贝)
 *
 * 注:strlen 是 IO 包装的基础依赖,已放在 io.h;此处只放纯字符串处理。
 * 依赖 io.h(trim 使用 strlen)。
 */

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

static inline void copy_str(char *dst, char *src, isize len) {
    for (int i = 0; i < len && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
}
