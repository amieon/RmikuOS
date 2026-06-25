#pragma once

/*
 * io.h —— 基础输入输出层。
 *
 * 内容:
 *   - strlen(字符串长度,IO 包装频繁使用,放在这里作为基础工具)
 *   - read / write(裸读写)
 *   - open / open2 / create / create2 / open_create / close(文件描述符开关)
 *   - puts / put_char(便捷输出)
 *
 * 只依赖 syscall 层。更高层的 fs.h(目录/stat)、fmt.h(格式化输出)
 * 都建立在这里的 write / strlen 之上。
 */

#include "syscall.h"

static inline usize strlen(const char *s) {
    usize n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static inline isize write(int fd, const char *buf, usize len) {
    return syscall3(SYS_WRITE, (usize)fd, (usize)buf, len);
}

static inline isize read(int fd, char *buf, usize len) {
    return syscall3(SYS_READ, (usize)fd, (usize)buf, len);
}

static inline void put_char(char ch) {
    write(1, &ch, 1);
}

static inline void puts(const char *s) {
    write(1, s, strlen(s));
}

static inline isize create2(const char *path, usize len) {
    return syscall3(SYS_CREATE, (usize)path, len, 0);
}

static inline isize create(const char *path) {
    return create2(path, strlen(path));
}

static inline isize open2(const char *path, usize len) {
    return syscall3(SYS_OPEN, (usize)path, len, 0);
}

static inline isize open(const char *path) {
    return open2(path, strlen(path));
}

static inline isize open_create(const char *path) {
    int fd = -1;
    if ((fd = open2(path, strlen(path))) < 0) {
        if (create2(path, strlen(path)) >= 0) {
            return open2(path, strlen(path));
        }
    }
    return fd;
}

static inline isize close(int fd) {
    return syscall3(SYS_CLOSE, (usize)fd, 0, 0);
}
