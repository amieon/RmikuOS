#ifndef RMIKU_UNISTD_H
#define RMIKU_UNISTD_H

/* POSIX <unistd.h> —— shell.c 需要的文件/进程原语。
 *
 * 重要：本文件不 include fs.h！fs.h 携带内核版 struct stat/struct dirent
 * 与 stat()/fstat()/rename()/mkdir() 等，与 <sys/stat.h>/<dirent.h> 的
 * POSIX 版本在同一个 TU 里会重定义冲突。这里全部走原始 syscall。
 * isatty()/access() 由 rmiku_shims.c 提供真实定义（extern）。
 */

#include "io.h"          /* read/write/close/open */
#include "process.h"     /* getpid/fork/sleep/exit/waitpid/getuid 等 */
#include "sys/types.h"
#include "errno.h"
#include "string.h"      /* strlen */
#include "time.h"        /* usleep/time */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* ---- rmiku_shims.c 提供真实定义 ---- */
extern int isatty(int);
extern int access(const char *path, int amode);

static inline int chdir(const char *path) {
    return (int)syscall3(SYS_CHDIR, (usize)path, strlen(path), 0);
}

static inline int unlink(const char *path) {
    return (int)syscall3(SYS_UNLINK, (usize)path, strlen(path), 0);
}

static inline int rmdir(const char *path) {
    return (int)syscall3(SYS_RMDIR, (usize)path, strlen(path), 0);
}

static inline int rename(const char *oldpath, const char *newpath) {
    return (int)syscall6(SYS_RENAME, (usize)oldpath, strlen(oldpath),
                         (usize)newpath, strlen(newpath), 0, 0);
}

static inline char *getcwd(char *buf, usize len) {
    isize r = syscall3(SYS_GETCWD, (usize)buf, len, 0);
    return (r < 0) ? (char *)0 : buf;
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

static inline int truncate(const char *path, off_t length) {
    return (int)syscall3(SYS_TRUNCATE, (usize)path, strlen(path), (usize)length);
}

static inline int chmod(const char *path, mode_t mode) {
    return (int)syscall3(SYS_CHMOD, (usize)path, strlen(path), (usize)mode);
}

static inline int chown(const char *path, uid_t uid, gid_t gid) {
    return (int)syscall6(SYS_CHOWN, (usize)path, strlen(path),
                         (usize)uid, (usize)gid, 0, 0);
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_UNISTD_H */
