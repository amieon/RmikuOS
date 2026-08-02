#ifndef RMIKU_UNISTD_H
#define RMIKU_UNISTD_H

/* POSIX <unistd.h> - thin wrapper over fs.h after Plan A unification.
 * chdir/getcwd/unlink/rmdir/rename/lseek/ftruncate/fsync/truncate/chmod/chown
 * are all provided by fs.h (POSIX signatures). Here we add isatty/access/
 * symlink/readlink (implemented in rmiku_shims.c) and SEEK_* constants. */

#include "fs.h"
#include "io.h"          /* read/write/close/open */
#include "process.h"     /* getpid/fork/sleep/exit/waitpid/getuid */
#include "time.h"        /* usleep/time */
#include "sys/types.h"
#include "errno.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* ---- implemented in rmiku_shims.c ---- */
extern int isatty(int);
extern int access(const char *path, int amode);

/* Symlink: not supported on RmikuOS (no symlinks in kernel FS).
 * symlink() always fails with ENOSYS; readlink() with EINVAL.
 * Implemented in rmiku_shims.c */
extern int symlink(const char *target, const char *linkpath);
extern ssize_t readlink(const char *path, char *buf, size_t bufsiz);

/* 全局环境指针(POSIX): crt0 启动时把 exec 传入的 envp 保存到这里 */
extern char **environ;

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_UNISTD_H */
