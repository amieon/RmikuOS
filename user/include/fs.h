#pragma once
#ifdef __cplusplus
extern "C" {
#endif


#include "io.h"
#include "sys/types.h"  
#define FILE_TYPE_FILE 1
#define FILE_TYPE_DIR  2

struct dirent {
    unsigned char file_type;   /* 1=文件 2=目录（内核值） */
    unsigned char name_len;    /* 名字长度, name 不带 NUL */
    unsigned char reserved[6];
    union {
        char name[56];         /* 内核字段名 */
        char d_name[56];       /* POSIX 别名（布局相同） */
    };
};

static inline isize getdents(int fd, struct dirent *buf, usize len) {
    return syscall3(SYS_GETDENTS, (usize)fd, (usize)buf, len);
}

/* ---- 工作目录 ---- */

static inline isize chdir2(const char *path, usize len) {
    return syscall3(SYS_CHDIR, (usize)path, len, 0);
}

static inline int chdir(const char *path) {
    return (int)chdir2(path, strlen(path));
}

/* POSIX 语义: 成功返回 buf, 失败返回 NULL */
static inline char *getcwd(char *buf, usize len) {
    if (syscall3(SYS_GETCWD, (usize)buf, len, 0) < 0) return (char *)0;
    return buf;
}

/* ---- 文件元信息 ---- */

#define STAT_TYPE_FILE 1
#define STAT_TYPE_DIR  2
#define STAT_TYPE_CHAR 3
#define STAT_TYPE_PIPE 4

/* ---- 文件类型位（POSIX, 并入 st_mode 高 4 位） ---- */
#define S_IFMT   0170000
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

/*
 * POSIX struct stat —— 用户态唯一结构。
 */
struct stat {
    mode_t   st_mode;       /* 权限位 | S_IF* 类型位 */
    usize    st_size;       /* 文件字节数 */
    uid_t    st_uid;        /* 属主 */
    gid_t    st_gid;        /* 属组 */
    time_t   st_mtime;      /* RmikuOS 无时间戳, 恒 0 */
    time_t   st_atime;      /* 恒 0 */
    time_t   st_ctime;      /* 恒 0 */
    long     st_blksize;    /* 近似值 512 */
    long     st_blocks;     /* 恒 0 */
};

/* 内核 Stat 的私有字节布局（32 字节, 必须与 kernel/src/fs/stat.rs 一致） */
typedef struct rk_stat_kernel {
    unsigned char file_type;   /* @0 */
    unsigned short mode;       /* @2 */
    unsigned int   uid;        /* @4 */
    unsigned int   gid;        /* @8 */
    usize          size;       /* @16 */
    unsigned char  reserved[4];/* @24 */
} rk_stat_kernel_t;

static inline int rk_stat_convert(const rk_stat_kernel_t *k, struct stat *st) {
    if (!st) return -1;
    st->st_size = (usize)k->size;
    st->st_uid  = (uid_t)k->uid;
    st->st_gid  = (gid_t)k->gid;
    st->st_mode = (mode_t)k->mode;
    /* 合并类型位: 内核 mode 只有权限位, 不合并则 S_ISDIR() 永远假 */
    switch (k->file_type) {
        case STAT_TYPE_DIR:  st->st_mode |= S_IFDIR; break;
        case STAT_TYPE_CHAR: st->st_mode |= S_IFCHR; break;
        case STAT_TYPE_PIPE: st->st_mode |= S_IFIFO; break;
        default:             st->st_mode |= S_IFREG; break;
    }
    st->st_mtime = 0; st->st_atime = 0; st->st_ctime = 0;
    st->st_blksize = 512; st->st_blocks = 0;
    return 0;
}

/* 把 st_mode 还原为 STAT_TYPE_* 码（1/2/3/4）。
 * 兼容旧代码里直接读 st.file_type 的写法:
 *   st.file_type == STAT_TYPE_DIR  →  stat_type_of(st.st_mode) == STAT_TYPE_DIR */
static inline int stat_type_of(mode_t m) {
    if (S_ISDIR(m))  return STAT_TYPE_DIR;
    if (S_ISCHR(m))  return STAT_TYPE_CHAR;
    if (S_ISFIFO(m)) return STAT_TYPE_PIPE;
    return STAT_TYPE_FILE;
}

static inline isize stat2(const char *path, usize len, struct stat *st) {
    rk_stat_kernel_t k;
    isize r = syscall3(SYS_STAT, (usize)path, len, (usize)&k);
    if (r < 0) return r;
    return (isize)rk_stat_convert(&k, st);
}

static inline int stat(const char *path, struct stat *st) {
    return (int)stat2(path, strlen(path), st);
}

/* 本 OS 无符号链接, lstat 与 stat 行为一致 */
static inline int lstat(const char *path, struct stat *st) {
    return stat(path, st);
}

static inline int fstat(int fd, struct stat *st) {
    rk_stat_kernel_t k;
    isize r = syscall3(SYS_FSTAT, (usize)fd, (usize)&k, 0);
    if (r < 0) return -1;
    return rk_stat_convert(&k, st);
}

/* chmod(path, mode): 修改文件权限位(低 12 位)。仅 root 或文件属主可改。 */
static inline isize chmod2(const char *path, usize len, usize mode) {
    return syscall3(SYS_CHMOD, (usize)path, len, mode);
}

static inline int chmod(const char *path, mode_t mode) {
    return (int)chmod2(path, strlen(path), (usize)mode);
}

/*
 * chown(path, uid, gid): 修改文件属主。简化模型下仅 root 可改。
 */
static inline isize chown2(const char *path, usize len, usize uid, usize gid) {
    return syscall6(SYS_CHOWN, (usize)path, len, uid, gid, 0, 0);
}

static inline int chown(const char *path, uid_t uid, gid_t gid) {
    return (int)chown2(path, strlen(path), (usize)uid, (usize)gid);
}

/* ---- 目录 / 文件增删 ---- */

/* POSIX 签名: mkdir(path, mode)。内核不记录新建目录的权限, mode 忽略。 */
static inline isize mkdir2(const char *path, usize len) {
    return syscall3(SYS_MKDIR, (usize)path, len, 0);
}

static inline int mkdir(const char *path, mode_t mode) {
    (void)mode;
    return (int)mkdir2(path, strlen(path));
}

static inline isize unlink2(const char *path, usize len) {
    return syscall3(SYS_UNLINK, (usize)path, len, 0);
}

static inline int unlink(const char *path) {
    return (int)unlink2(path, strlen(path));
}

static inline isize rmdir2(const char *path, usize len) {
    return syscall3(SYS_RMDIR, (usize)path, len, 0);
}

static inline int rmdir(const char *path) {
    return (int)rmdir2(path, strlen(path));
}

static inline isize remove_recursive2(const char *path, usize len) {
    return syscall3(SYS_REMOVE_RECURSIVE, (usize)path, len, 0);
}

static inline isize remove_recursive(const char *path) {
    return remove_recursive2(path, strlen(path));
}

static inline int fcntl(int fd, int cmd, int arg) {
    return (int)syscall3(SYS_FCNTL, (usize)fd, (usize)cmd, (usize)arg);
}

static inline off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)syscall3(SYS_LSEEK, (usize)fd, (usize)offset, (usize)whence);
}

static inline int ftruncate(int fd, off_t length) {
    return (int)syscall3(SYS_FTRUNCATE, (usize)fd, (usize)length, 0);
}

static inline int fsync(int fd) {
    return (int)syscall3(SYS_FSYNC, (usize)fd, 0, 0);
}

static inline isize truncate2(const char *path, usize len, usize length) {
    return syscall3(SYS_TRUNCATE, (usize)path, len, length);
}

static inline int truncate(const char *path, off_t length) {
    return (int)truncate2(path, strlen(path), (usize)length);
}

static inline isize rename2(const char *oldpath, usize oldlen, const char *newpath, usize newlen) {
    return syscall6(SYS_RENAME, (usize)oldpath, oldlen, (usize)newpath, newlen, 0, 0);
}

static inline int rename(const char *oldpath, const char *newpath) {
    return (int)rename2(oldpath, strlen(oldpath), newpath, strlen(newpath));
}

#ifdef __cplusplus
}
#endif
