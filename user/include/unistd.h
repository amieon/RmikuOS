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

/* gethostname: 没有 hostname syscall，返回编译期常量（与 uname().nodename 一致）。
 * 移植代码（git 的 ident.c）用它拼默认邮箱，拿不到才更要显式配置 user.email。 */
static inline int gethostname(char *name, unsigned long len) {
    static const char host[] = "rmikuos";
    if (!name || len == 0) return -1;
    unsigned long i = 0;
    while (host[i] && i + 1 < len) { name[i] = host[i]; i++; }
    name[i] = '\0';
    return 0;
}

/* ---- sysconf ----
 * 只实现真正有意义的三个（编号与 glibc 一致，方便对照）：
 *   _SC_PAGESIZE         内核用 4KiB 页
 *   _SC_OPEN_MAX         进程 fd 上限（kernel/src/task/manager_wrapper.rs: MAX_FD_PER_PROCESS）
 *   _SC_NPROCESSORS_ONLN 内核没有暴露"在线 hart 数"的接口，保守返回 1
 *                       （调用方会据此少开线程，不会因此算错）
 * 其余一律 -1 + EINVAL，不返回瞎编的数字。 */
#define _SC_OPEN_MAX          4
#define _SC_PAGESIZE         30
#define _SC_NPROCESSORS_ONLN 84

static inline long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE:         return 4096;
    case _SC_OPEN_MAX:         return 1024;
    case _SC_NPROCESSORS_ONLN: return 1;
    default: errno = EINVAL;   return -1;
    }
}

/* alarm：内核没有"定时给进程发信号"的机制，这里只能不装。
 * 返回 0（= 没有未决闹钟）。依赖它的超时逻辑会退化成"永不超时"，
 * 而不是"立刻超时"——对 git 的 upload-pack/daemon 是可接受的退化。 */
static inline unsigned int alarm(unsigned int seconds) {
    (void)seconds;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_UNISTD_H */
