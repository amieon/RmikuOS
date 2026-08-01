#ifndef RMIKU_SYS_STAT_H
#define RMIKU_SYS_STAT_H

/* POSIX <sys/stat.h> —— 为 sqlite3 shell 提供 struct stat 与 stat/lstat/fstat。
 *
 * 设计要点（务必遵守）：
 *   1. 绝不能 include fs.h。fs.h 的 struct stat 是内核字节布局
 *      （字段名 mode/size，无 st_ 前缀），而这里需要 POSIX 布局
 *      （st_mode/st_size/st_mtime 等）。两者同名 struct stat，同 TU
 *      包含会直接重定义冲突。
 *   2. 本文件自包含：通过 syscall.h 直接打 SYS_STAT/SYS_FSTAT/SYS_MKDIR，
 *      内核返回的 32 字节 Stat 先落到私有 rk_stat_kernel_t，再翻译成
 *      POSIX struct stat，并顺手把 file_type 合并成 S_IF* 类型位
 *      （内核 mode 字段只含权限位，不合并则 S_ISDIR() 永远为假）。
 *   3. RmikuOS 无时间戳记录（内核 Stat.reserved 恒为 0），
 *      st_mtime/st_atime/st_ctime 诚实返回 0。
 */

#include "sys/types.h"   /* mode_t / uid_t / gid_t / time_t / off_t */
#include "errno.h"
#include "syscall.h"
#include "string.h"      /* strlen */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 文件类型位（与 POSIX 一致, 合并进 st_mode 高 4 位） ---- */
#ifndef S_IFMT
#define S_IFMT   0170000
#endif
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* ---- 权限位（与 fs.h / 内核常量一致；独立定义以免依赖 fs.h） ---- */
#ifndef S_IRWXU
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
#endif

/* ---- POSIX struct stat（shell.c 只用 st_mode/st_size/st_mtime） ---- */
struct stat {
    mode_t   st_mode;
    usize    st_size;
    uid_t    st_uid;
    gid_t    st_gid;
    time_t   st_mtime;
    time_t   st_atime;
    time_t   st_ctime;
    long     st_blksize;
    long     st_blocks;
};

/* ---- 内核 Stat 的私有字节布局（必须与 kernel/src/fs/stat.rs 一致, 32 字节） ---- */
typedef struct rk_stat_kernel {
    unsigned char file_type;   /* @0 */
    unsigned short mode;       /* @2 */
    unsigned int   uid;        /* @4 */
    unsigned int   gid;        /* @8 */
    usize          size;       /* @16 */
    unsigned char  reserved[4];/* @24 */
} rk_stat_kernel_t;

#define RK_STAT_TYPE_FILE 1
#define RK_STAT_TYPE_DIR  2
#define RK_STAT_TYPE_CHAR 3
#define RK_STAT_TYPE_PIPE 4

static inline int rk_stat_convert(const rk_stat_kernel_t *k, struct stat *st) {
    if (!st) return -1;
    st->st_size = (usize)k->size;
    st->st_uid  = (uid_t)k->uid;
    st->st_gid  = (gid_t)k->gid;
    st->st_mode = (mode_t)k->mode;
    /* 合并文件类型位: 内核 mode 只有权限位, 不合并则 S_ISDIR 等全假 */
    switch (k->file_type) {
        case RK_STAT_TYPE_DIR:  st->st_mode |= S_IFDIR; break;
        case RK_STAT_TYPE_CHAR: st->st_mode |= S_IFCHR; break;
        case RK_STAT_TYPE_PIPE: st->st_mode |= S_IFIFO; break;
        default:                st->st_mode |= S_IFREG; break;
    }
    /* RmikuOS 不记录时间戳 */
    st->st_mtime = 0; st->st_atime = 0; st->st_ctime = 0;
    st->st_blksize = 512; st->st_blocks = 0;
    return 0;
}

static inline int stat(const char *path, struct stat *st) {
    rk_stat_kernel_t k;
    isize r = syscall3(SYS_STAT, (usize)path, strlen(path), (usize)&k);
    if (r < 0) { errno = ENOENT; return -1; }
    return rk_stat_convert(&k, st);
}

/* 本 OS 无符号链接, lstat 与 stat 行为一致 */
static inline int lstat(const char *path, struct stat *st) {
    return stat(path, st);
}

static inline int fstat(int fd, struct stat *st) {
    rk_stat_kernel_t k;
    isize r = syscall3(SYS_FSTAT, (usize)fd, (usize)&k, 0);
    if (r < 0) { errno = EBADF; return -1; }
    return rk_stat_convert(&k, st);
}

static inline int mkdir(const char *path, mode_t mode) {
    (void)mode;   /* 内核不记录新建目录的权限 */
    isize r = syscall3(SYS_MKDIR, (usize)path, strlen(path), 0);
    if (r < 0) { errno = EEXIST; return -1; }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_SYS_STAT_H */
