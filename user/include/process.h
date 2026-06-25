#pragma once


#include "io.h"

static inline isize yield(void) {
    return syscall3(SYS_YIELD, 0, 0, 0);
}

static inline isize getpid(void) {
    return syscall3(SYS_GETPID, 0, 0, 0);
}

static inline isize fork(void) {
    return syscall3(SYS_FORK, 0, 0, 0);
}

static inline isize waitpid(isize pid, int *exit_code) {
    return syscall3(SYS_WAITPID, (usize)pid, (usize)exit_code, 0);
}

static inline isize sleep(usize ticks) {
    return syscall3(SYS_SLEEP, ticks, 0, 0);
}

static inline void exit(int code) {
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
