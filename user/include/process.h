#pragma once
#ifdef __cplusplus
extern "C" {
#endif


#include "io.h"
#include "file.h"   /* fflush(stdout/stderr): exit 前必须冲刷缓冲! */
#include "env.h"    /* getenv: execvp 需要 PATH */
#define WNOHANG 1
#define WUNTRACED 0

/* wait status 编码：内核给的是 exit_code << 8（shell 的 $? 就是 exit_code）。
 *
 * 与 Linux 的差别：Linux 把"被信号杀死"写在低 7 位（0x000b = SIGSEGV），
 * 而本内核直接把信号死亡**折算成退出码 128+sig**（139 = SIGSEGV）。
 * 所以判定只能用退出码大小，不能照抄 Linux 的位掩码：
 *   exit_code <  128 → 正常退出
 *   exit_code >= 128 → 被信号 (exit_code - 128) 杀死
 * 代价：程序自己 exit(200) 也会被判成"被信号 72 杀死"。教学系统接受，
 * 真正要区分就得让内核改 wait status 编码（另一次 ABI 变更）。 */
#define RK_WAIT_EXIT_CODE(status) (((status) >> 8) & 0xFF)
#define WEXITSTATUS(status)  RK_WAIT_EXIT_CODE(status)
#define WIFEXITED(status)    (RK_WAIT_EXIT_CODE(status) < 128)
#define WIFSIGNALED(status)  (RK_WAIT_EXIT_CODE(status) >= 128)
#define WTERMSIG(status)     (RK_WAIT_EXIT_CODE(status) - 128)
#define WCOREDUMP(status)    (0)   /* 内核不产生 core，恒 0 */

static inline isize yield(void) {
    return syscall3(SYS_YIELD, 0, 0, 0);
}

static inline isize getpid(void) {
    return syscall3(SYS_GETPID, 0, 0, 0);
}

static inline isize getppid(void) {
    return syscall3(SYS_GETPPID, 0, 0, 0);
}

/* 把 pid 设为前台进程(Ctrl+C 投递目标)。shell 启动后台任务后用它夺回前台。 */
static inline isize set_front(isize pid) {
    return syscall3(SYS_SET_FRONT, (usize)pid, 0, 0);
}


static inline isize fork(void) {
    return syscall3(SYS_FORK, 0, 0, 0);
}

static inline isize waitpid(isize pid, int *exit_code, usize option) {
    return syscall3(SYS_WAITPID, (usize)pid, (usize)exit_code, option);
}

static inline isize wait(int *exit_code) {
    return syscall3(SYS_WAITPID, -1, (usize)exit_code, WUNTRACED);
}

static inline isize sleep(usize ticks) {
    return syscall3(SYS_SLEEP, ticks, 0, 0);
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

static inline void exit(int code) {
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

static inline int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)path; (void)argv; (void)envp;
    /* 内核的 exec ABI 没有 envp：环境由 shell/crt0 的 environ 继承。
     * POSIX 语义要求 execve 用 envp **替换**环境，这里做不到，
     * 所以明确失败而不是静默忽略参数。 */
    errno = ENOSYS;
    return -1;
}

/* _exit：不跑 atexit、不冲刷 stdio（与 exit 的关键区别）。
 * fork 出来的子进程在 exec 失败后应该用它，避免把父进程的缓冲区复制一份再刷出去。 */
static inline void _exit(int code) {
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
