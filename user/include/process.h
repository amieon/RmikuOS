#pragma once
#ifdef __cplusplus
extern "C" {
#endif


#include "io.h"
#define WNOHANG 1
#define WUNTRACED 0

#define WEXITSTATUS(status)  (((status) >> 8) & 0xFF)

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
