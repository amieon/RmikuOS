#pragma once
#ifdef __cplusplus
extern "C" {
#endif


#include "io.h"

/* ---- 目录项 ---- */

#define FILE_TYPE_FILE 1
#define FILE_TYPE_DIR  2

struct dirent {
    unsigned char file_type;
    unsigned char name_len;
    unsigned char reserved[6];
    char name[56];
};

static inline isize getdents(int fd, struct dirent *buf, usize len) {
    return syscall3(SYS_GETDENTS, (usize)fd, (usize)buf, len);
}

/* ---- 工作目录 ---- */

static inline isize chdir2(const char *path, usize len) {
    return syscall3(SYS_CHDIR, (usize)path, len, 0);
}

static inline isize chdir(const char *path) {
    return chdir2(path, strlen(path));
}

static inline isize getcwd(char *buf, usize len) {
    return syscall3(SYS_GETCWD, (usize)buf, len, 0);
}

/* ---- 文件元信息 ---- */

#define STAT_TYPE_FILE 1
#define STAT_TYPE_DIR  2
#define STAT_TYPE_CHAR 3

struct stat {
    unsigned char file_type;
    unsigned char reserved[7];
    usize size;
};

static inline isize stat2(const char *path, usize len, struct stat *st) {
    return syscall3(SYS_STAT, (usize)path, len, (usize)st);
}

static inline isize stat(const char *path, struct stat *st) {
    return stat2(path, strlen(path), st);
}

static inline isize fstat(int fd, struct stat *st) {
    return syscall3(SYS_FSTAT, (usize)fd, (usize)st, 0);
}

/* ---- 目录 / 文件增删 ---- */

static inline isize mkdir2(const char *path, usize len) {
    return syscall3(SYS_MKDIR, (usize)path, len, 0);
}

static inline isize mkdir(const char *path) {
    return mkdir2(path, strlen(path));
}

static inline isize unlink2(const char *path, usize len) {
    return syscall3(SYS_UNLINK, (usize)path, len, 0);
}

static inline isize unlink(const char *path) {
    return unlink2(path, strlen(path));
}

static inline isize rmdir2(const char *path, usize len) {
    return syscall3(SYS_RMDIR, (usize)path, len, 0);
}

static inline isize rmdir(const char *path) {
    return rmdir2(path, strlen(path));
}

static inline isize remove_recursive2(const char *path, usize len) {
    return syscall3(SYS_REMOVE_RECURSIVE, (usize)path, len, 0);
}

static inline isize remove_recursive(const char *path) {
    return remove_recursive2(path, strlen(path));
}
#ifdef __cplusplus
}
#endif
