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

/*
 * 必须与内核 kernel/src/fs/stat.rs 的 Stat(repr(C)) 字节布局完全一致,
 * 因为 sys_stat / sys_fstat 直接把 size_of(Stat) 个字节拷到用户态。
 *
 *   file_type : u8   @0
 *   mode      : u16  @2  (1 字节对齐填充)
 *   uid       : u32  @4
 *   gid       : u32  @8
 *   size      : usize @16 (4 字节对齐填充)
 *   reserved  : u8[4] @24
 *   总大小 32 字节(8 字节对齐)。
 */
struct stat {
    unsigned char file_type;
    unsigned short mode;
    unsigned int uid;
    unsigned int gid;
    usize size;
    unsigned char reserved[4];
};

/* ---- 权限位(低 12 位, 与内核 S_* 常量一致)。注意: C 八进制用前导 0, 不是 0o。 ---- */
#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 0070
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXO 0007
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000

static inline isize stat2(const char *path, usize len, struct stat *st) {
    return syscall3(SYS_STAT, (usize)path, len, (usize)st);
}

static inline isize stat(const char *path, struct stat *st) {
    return stat2(path, strlen(path), st);
}

static inline isize fstat(int fd, struct stat *st) {
    return syscall3(SYS_FSTAT, (usize)fd, (usize)st, 0);
}


/* chmod(path, mode): 修改文件权限位(低 12 位)。仅 root 或文件属主可改。 */
static inline isize chmod2(const char *path, usize len, usize mode) {
    return syscall3(SYS_CHMOD, (usize)path, len, mode);
}

static inline isize chmod(const char *path, usize mode) {
    return chmod2(path, strlen(path), mode);
}

/*
 * chown(path, uid, gid): 修改文件属主。简化模型下仅 root 可改。
 */
static inline isize chown2(const char *path, usize len, usize uid, usize gid) {
    return syscall6(SYS_CHOWN, (usize)path, len, uid, gid, 0, 0);
}

static inline isize chown(const char *path, usize uid, usize gid) {
    return chown2(path, strlen(path), uid, gid);
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

static inline isize fcntl(isize fd, isize cmd, isize arg) {
    return syscall3(SYS_FCNTL, (usize)fd, (usize)cmd, (usize)arg);
}


static inline isize lseek(isize fd, isize offset, usize whence) {
    return syscall3(SYS_LSEEK, (usize)fd, (usize)offset, whence);
}


static inline isize ftruncate(isize fd, usize length) {
    return syscall3(SYS_FTRUNCATE, (usize)fd, length, 0);
}


static inline isize fsync(isize fd) {
    return syscall3(SYS_FSYNC, (usize)fd, 0, 0);
}


static inline isize truncate2(const char *path, usize len, usize length) {
    return syscall3(SYS_TRUNCATE, (usize)path, len, length);
}
static inline isize truncate(const char *path, usize length) {
    return truncate2(path, strlen(path), length);
}

static inline isize rename2(const char *oldpath, usize oldlen, const char *newpath, usize newlen) {
    return syscall4(SYS_RENAME, (usize)oldpath, oldlen, (usize)newpath, newlen);
}
static inline isize rename(const char *oldpath, const char *newpath) {
    return rename2(oldpath, strlen(oldpath), newpath, strlen(newpath));
}

#ifdef __cplusplus
}
#endif
