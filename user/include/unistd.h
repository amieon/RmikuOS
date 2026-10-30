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
#include "syscall.h"     /* SYS_GETPAGESIZE */
#include "ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* ---- isatty 在 lib/string.c（weak）----
 * 需要 fstat 判设备号，放头文件里就得让每个 TU 都拉 fs.h；而且它是 weak 的，
 * 移植代码自带实现时能让位。 */
extern int isatty(int);

/* 标准流文件描述符(kilo 等需要) */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

extern int access(const char *path, int amode);

/* symlink/readlink/link/lstat: 见 fs.h（内核 SYS_SYMLINK/READLINK/LINK/LSTAT）。 */

/* 全局环境指针(POSIX): crt0 启动时把 exec 传入的 envp 保存到这里。*/
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

/* ---- getpagesize / sysconf ----
 * getpagesize() 是 BSD 名字，POSIX 的正规写法是 sysconf(_SC_PAGESIZE)。
 *
 * 页大小只在内核定义一次（kernel/src/mm/config.rs 的 PAGE_SIZE），
 * 用户态通过 98 号系统调用问出来：这样以后换页大小只改内核一处，
 * 不会出现"内核改了、libc 还写死 4096"这种没有编译错误、只会在运行时
 * 表现成 mmap 未对齐的怪 bug。
 *
 * 缓存放文件作用域 —— 函数内可变 static 会让 TCC 生成原子守卫，这是本项目
 * 已知雷区（见 fs.h 里 mkstemp 那段注释）。
 *
 * 兜底 4096：只有"跑的是还没有 98 号的老内核"时才走到。那种内核的页大小
 * 本来就是 4KiB，所以这不是猜，而是对老内核的正确描述。
 */
static usize __rk_pagesize_cache = 0;

static inline int getpagesize(void) {
    if (__rk_pagesize_cache == 0) {
        isize r = syscall3(SYS_GETPAGESIZE, 0, 0, 0);
        __rk_pagesize_cache = (r > 0) ? (usize)r : 4096;
    }
    return (int)__rk_pagesize_cache;
}

/* ---- sysconf ----
 * 只实现真正有意义的三个（编号与 glibc 一致，方便对照）：
 *   _SC_PAGESIZE         内核页大小（问 98 号，不写死）
 *   _SC_OPEN_MAX         进程 fd 上限（kernel/src/task/manager_wrapper.rs: MAX_FD_PER_PROCESS）
 *   _SC_NPROCESSORS_ONLN 内核没有暴露"在线 hart 数"的接口，保守返回 1
 *                       （调用方会据此少开线程，不会因此算错）
 * 其余一律 -1 + EINVAL，不返回瞎编的数字。 */
#define _SC_OPEN_MAX          4
#define _SC_PAGESIZE         30
/* POSIX 把这两个名字当同义词（Linux 上值也相同）。git 的 wrapper.c 用的是
 * 带下划线的 _SC_PAGE_SIZE，只在 `#ifdef _SC_PAGE_SIZE` 里取值 ——
 * 不定义也不会崩，但会静默退回硬编码的 4096，页大小一变就错。 */
#define _SC_PAGE_SIZE        _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN 84

static inline long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE:         return (long)getpagesize();
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

/* getrandom：内核熵源（virtio-rng）的唯一出口。
 *
 * 语义对齐 Linux：返回实际写入的字节数，失败返回 -1 并置 errno。
 * 关键差别只有一处：Linux 在熵池未初始化时会阻塞，本内核没有"等熵池"
 * 这个状态，设备不在就直接 EAGAIN，两种 flags 表现相同。
 */
static inline ssize_t getrandom(void *buf, usize count, unsigned int flags) {
    return rk_sys_ret(syscall3(SYS_GETRANDOM, (usize)buf, count, (usize)flags));
}

#ifdef __cplusplus
}
#endif

#endif /* RMIKU_UNISTD_H */
