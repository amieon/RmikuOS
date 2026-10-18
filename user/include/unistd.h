#ifndef RMIKU_UNISTD_H
#define RMIKU_UNISTD_H

/* POSIX <unistd.h> - thin wrapper over fs.h after Plan A unification.
 * chdir/getcwd/unlink/rmdir/rename/lseek/ftruncate/fsync/truncate/chmod/chown
 * are all provided by fs.h (POSIX signatures).
 * symlink/readlink/link/lstat 也已由 fs.h 的 header-only syscall wrapper 提供；
 * 这里只补 isatty/access 和 SEEK_* 常量。 */

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

/* 标准流文件描述符(kilo 等需要) */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

extern int access(const char *path, int amode);

/* symlink/readlink/link/lstat: 见 fs.h（内核 SYS_SYMLINK/READLINK/LINK/LSTAT）。 */

/* 全局环境指针(POSIX): crt0 启动时把 exec 传入的 envp 保存到这里 */
extern char **environ;

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_UNISTD_H */
