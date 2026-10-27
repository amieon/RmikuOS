#pragma once
#ifdef __cplusplus
extern "C" {
#endif


#include "io.h"
#include "file.h"   /* fflush(stdout/stderr): exit 前必须冲刷缓冲! */
#include "env.h"    /* getenv: execvp 需要 PATH */
#define WNOHANG    1
/* 也报告"被停止"的子进程。2 是 POSIX 的值，以前这里写成 0 —— 那等于
 * 把 WUNTRACED 悄悄变成"不报告"，Ctrl+Z 之后 shell 永远收不到消息。 */
#define WUNTRACED  2
/* 也报告"被 SIGCONT 继续"的子进程。 */
#define WCONTINUED 8

/* wait status 是 POSIX 的 16 位编码（内核在 manager.rs 的 try_waitpid 里
 * 从内部 exit_code 转换后写回，见那里的注释）：
 *
 *   bits 15..8   退出码（正常退出）/ 暂停信号（WIFSTOPPED）
 *   bit  7       core dump 标记
 *   bits 6..0    终止信号号；0 表示正常退出
 *
 * 三种结局互斥可辨：
 *   WIFEXITED   低 7 位 == 0
 *   WIFSIGNALED 低 7 位非 0，且不是 0x7f
 *   WIFSTOPPED  低 8 位 == 0x7f   ← 0x7f 是留给"暂停"的标签
 *
 * 为什么不能图省事写成 `128 + sig`：那样 `exit(139)` 和"被 SIGSEGV 杀死"
 * 是同一个数，父进程无从区分；core dump 位与暂停态也无处安放。
 * 而且一旦压平成 0..255，信息就永久丢了 —— 所以编码必须在 syscall 边界做。
 *
 * 顺带解释 exit 码为什么只有 0..255：不是语言限制，是这 8 位位宽决定的。 */
#define WIFEXITED(status)   (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status)    ((status) & 0x7f)
#define WCOREDUMP(status)   (((status) & 0x80) != 0)
#define WIFSTOPPED(status)  (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status)    (((status) >> 8) & 0xff)
/* 继续事件的编码是 0xffff（Linux 也是这个值），和"停止"（低 8 位 0x7f）
 * 靠低字节区分：0xff != 0x7f。 */
#define WIFCONTINUED(status) ((status) == 0xffff)

static inline isize yield(void) {
    return syscall3(SYS_YIELD, 0, 0, 0);
}

static inline isize getpid(void) {
    return syscall3(SYS_GETPID, 0, 0, 0);
}

static inline isize getppid(void) {
    return syscall3(SYS_GETPPID, 0, 0, 0);
}

static inline isize getpgid(isize pid) {
    return syscall3(SYS_GETPGID, (usize)pid, 0, 0);
}

static inline isize getpgrp(void) {
    return getpgid(0);
}

static inline int setpgid(isize pid, isize pgid) {
    return (int)syscall3(SYS_SETPGID, (usize)pid, (usize)pgid, 0);
}

/* 把 pid 设为前台进程(Ctrl+C 投递目标)。shell 启动后台任务后用它夺回前台。 */
static inline isize set_front(isize pid) {
    return syscall3(SYS_SET_FRONT, (usize)pid, 0, 0);
}


static inline isize fork(void) {
    return syscall3(SYS_FORK, 0, 0, 0);
}

/* 返回值要过 rk_sys_ret：阻塞中被信号打断时内核给的是 -EINTR，
 * 用户态必须看到 POSIX 的 -1 + errno == EINTR（否则 `while (errno == EINTR)`
 * 那套重试逻辑全都对不上）。rk_sys_ret 来自 io.h，后者在本头之前被 user.h 包含。 */
static inline isize waitpid(isize pid, int *exit_code, usize option) {
    return rk_sys_ret(syscall3(SYS_WAITPID, (usize)pid, (usize)exit_code, option));
}

/* wait() 等价于 waitpid(-1, st, 0)：不报停止事件。
 * （以前这里传的是 WUNTRACED，只是当时它被定义成 0 才显得没事。） */
static inline isize wait(int *exit_code) {
    return rk_sys_ret(syscall3(SYS_WAITPID, -1, (usize)exit_code, 0));
}

/* 被信号提前叫醒时返回 -1 + EINTR（POSIX nanosleep 语义）。 */
static inline isize sleep(usize ticks) {
    return rk_sys_ret(syscall3(SYS_SLEEP, ticks, 0, 0));
}

/* ---- atexit / exit: 注册退出回调, exit() 时逆序调用 ---- */
#define ATEXIT_MAX 32
static void (*_atexit_funcs[ATEXIT_MAX])(void);
static int _atexit_n = 0;

static inline int atexit(void (*func)(void)) {
    if (!func || _atexit_n >= ATEXIT_MAX) return -1;
    _atexit_funcs[_atexit_n++] = func;
    return 0;
}

static inline __attribute__((noreturn)) void exit(int code) {
    while (_atexit_n > 0) _atexit_funcs[--_atexit_n]();
    /* 必须先冲刷标准流缓冲再退出: 否则 printf 等写入 stdout 缓冲的
     * 内容(如 abort 的 "abort\n"、lua panic 前的部分输出)会随进程
     * 直接 syscall 退出而丢失。crt0 链接的是 string.c 的 weak exit,
     * 但源码内联调用的 exit() 是这里的 static 版本, 必须同样 flush。 */
    fflush(stdout);
    fflush(stderr);
    syscall3(SYS_EXIT, (usize)code, 0, 0);
    for (;;) {}
}

/* ---- exec ---- */

#define EXEC_MAX_ARGS 8

struct user_arg {
    const char *ptr;
    usize len;
};

struct exec_args {
    usize argc;
    struct user_arg argv[EXEC_MAX_ARGS];
};

static inline isize exec2(const char *name, usize len) {
    return syscall3(SYS_EXEC, (usize)name, len, 0);
}

static inline isize exec_with_args(const char *path, struct exec_args *args) {
    return syscall3(SYS_EXEC, (usize)path, strlen(path), (usize)args);
}

static inline isize exec(const char *path) {
    struct exec_args args;
    args.argc = 1;
    args.argv[0].ptr = path;
    args.argv[0].len = strlen(path);

    for (int i = 1; i < EXEC_MAX_ARGS; i++) {
        args.argv[i].ptr = 0;
        args.argv[i].len = 0;
    }

    return exec_with_args(path, &args);
}

/* ---- POSIX exec 家族 ----
 * 内核的 SYS_EXEC 只接收 (path, struct exec_args*)，argv 上限 EXEC_MAX_ARGS=8。
 * 所以 execv/execvp 在用户态把 char *const argv[] 转成 exec_args。
 *
 * 三个必须知道的边界：
 *   1. 超过 8 个参数 → E2BIG（不是截断！截断会让子进程收到错的参数）
 *   2. execve 的 envp 没有传递通道 → 明确 ENOSYS，不假装设置环境
 *   3. exec* 成功不返回；返回就一定是失败 */
static inline int execv(const char *path, char *const argv[]) {
    if (!path) { errno = ENOENT; return -1; }

    struct exec_args args;
    usize n = 0;
    if (argv) {
        while (argv[n]) n++;
    }
    if (n > EXEC_MAX_ARGS) { errno = E2BIG; return -1; }

    args.argc = n;
    for (usize i = 0; i < n; i++) {
        args.argv[i].ptr = argv[i];
        args.argv[i].len = strlen(argv[i]);
    }
    for (usize i = n; i < EXEC_MAX_ARGS; i++) {
        args.argv[i].ptr = 0;
        args.argv[i].len = 0;
    }

    (void)exec_with_args(path, &args);
    return -1;                      /* 返回到这里 = 执行失败 */
}

/* execvp：名字不含 '/' 时按 PATH 逐个目录试。
 * 注意"每个候选都失败"才返回，所以 errno 取最后一次尝试的值——POSIX 允许
 * （Linux 也是这个行为），但对"文件不存在"应统一报 ENOENT。 */
static inline int execvp(const char *file, char *const argv[]) {
    if (!file || !*file) { errno = ENOENT; return -1; }
    if (strchr(file, '/')) return execv(file, argv);

    const char *path = getenv("PATH");
    if (!path || !*path) path = "/bin:/tests:/programs";

    char buf[256];
    const usize flen = strlen(file);

    while (1) {
        const char *end = strchr(path, ':');
        usize dlen = end ? (usize)(end - path) : strlen(path);

        if (dlen + 1 + flen + 1 <= sizeof buf) {
            for (usize i = 0; i < dlen; i++) buf[i] = path[i];
            buf[dlen] = '/';
            for (usize i = 0; i <= flen; i++) buf[dlen + 1 + i] = file[i];
            execv(buf, argv);       /* 成功不返回；失败继续下一个目录 */
        }

        if (!end) break;
        path = end + 1;
    }

    errno = ENOENT;
    return -1;
}

/* execl / execlp：变参版本。argv 由 `...` 里的指针序列组成，以 NULL 结尾。。 */
static inline int execl(const char *path, const char *arg, ...) {
    char *argv[EXEC_MAX_ARGS + 1];
    usize n = 0;

    va_list ap;
    va_start(ap, arg);
    const char *a = arg;
    while (a) {
        if (n >= EXEC_MAX_ARGS) {
            va_end(ap);
            errno = E2BIG;
            return -1;
        }
        argv[n++] = (char *)a;
        a = va_arg(ap, const char *);
    }
    va_end(ap);
    argv[n] = 0;

    return execv(path, argv);
}

static inline int execlp(const char *file, const char *arg, ...) {
    char *argv[EXEC_MAX_ARGS + 1];
    usize n = 0;

    va_list ap;
    va_start(ap, arg);
    const char *a = arg;
    while (a) {
        if (n >= EXEC_MAX_ARGS) {
            va_end(ap);
            errno = E2BIG;
            return -1;
        }
        argv[n++] = (char *)a;
        a = va_arg(ap, const char *);
    }
    va_end(ap);
    argv[n] = 0;

    return execvp(file, argv);
}

static inline int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)path; (void)argv; (void)envp;
    /* 内核的 exec ABI 没有 envp：环境由 shell/crt0 的 environ 继承。
     * POSIX 语义要求 execve 用 envp 替换环境，这里做不到，
     * 所以明确失败而不是静默忽略参数。 */
    errno = ENOSYS;
    return -1;
}

/* _exit：不跑 atexit、不冲刷 stdio（与 exit 的关键区别）。
 * fork 出来的子进程在 exec 失败后应该用它，避免把父进程的缓冲区复制一份再刷出去。 */
static inline __attribute__((noreturn)) void _exit(int code) {
    syscall3(SYS_EXIT, (usize)code, 0, 0);
    for (;;) {}
}

static inline isize getuid(void)  { return syscall3(SYS_GETUID, 0, 0, 0); }
static inline isize geteuid(void) { return syscall3(SYS_GETEUID, 0, 0, 0); }
static inline isize getgid(void)  { return syscall3(SYS_GETGID, 0, 0, 0); }
static inline isize getegid(void) { return syscall3(SYS_GETEGID, 0, 0, 0); }

#define UID_NO_CHANGE ((usize)-1)   /* setreuid/setregid 的 -1 哨兵 */
#define GID_NO_CHANGE ((usize)-1)

static inline isize setuid(usize uid)     { return syscall3(SYS_SETUID, uid, 0, 0); }
static inline isize seteuid(usize euid)   { return syscall3(SYS_SETEUID, euid, 0, 0); }
static inline isize setgid(usize gid)     { return syscall3(SYS_SETGID, gid, 0, 0); }
static inline isize setegid(usize egid)   { return syscall3(SYS_SETEGID, egid, 0, 0); }
static inline isize setreuid(usize ruid, usize euid) { return syscall3(SYS_SETREUID, ruid, euid, 0); }
static inline isize setregid(usize rgid, usize egid) { return syscall3(SYS_SETREGID, rgid, egid, 0); }

/* 附加组。内核 getgroups(size, list): size==0 返回组数; size>=ngroups 时
   把组 id 写入 list 并返回组数; size<ngroups 返回 -1。setgroups(size, list)
   仅特权进程(euid==0)可调用, 失败返回 -1。 */
static inline isize getgroups(usize size, usize *list) {
    return syscall3(SYS_GETGROUPS, size, (usize)list, 0);
}

static inline isize setgroups(usize size, const usize *list) {
    return syscall3(SYS_SETGROUPS, size, (usize)list, 0);
}

#ifdef __cplusplus
}
#endif
