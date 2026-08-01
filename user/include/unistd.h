#ifndef RMIKU_UNISTD_H
#define RMIKU_UNISTD_H

/* POSIX <unistd.h> —— 方案 A 统一后是 fs.h 的薄壳。
 * chdir/getcwd/unlink/rmdir/rename/lseek/ftruncate/fsync/truncate/chmod/chown
 * 全部由 fs.h 提供（POSIX 签名）。这里补 isatty/access（rmiku_shims.c
 * 实现）与 SEEK_* 常量。 */

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

/* ---- rmiku_shims.c 提供真实定义 ---- */
extern int isatty(int);
extern int access(const char *path, int amode);

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_UNISTD_H */
