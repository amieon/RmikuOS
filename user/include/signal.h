#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "types.h"
#include "syscall.h"
#include "process.h"

#define SIGINT  2
#define SIGILL  4
#define SIGABRT 6
#define SIGFPE  8
#define SIGKILL 9
#define SIGTERM 15

typedef void (*sighandler_t)(int);

#define SIG_DFL   ((sighandler_t)0)
#define SIG_IGN   ((sighandler_t)1)
#define SIG_ERR   ((sighandler_t)-1)

static inline void kill(isize pid, usize sig){
    syscall3(SYS_KILL, pid, sig, 0);
}

/* 没有 per-process handler 表，假装注册成功 */
static inline sighandler_t signal(int sig, sighandler_t handler) {
    (void)sig; (void)handler;
    return SIG_DFL;
}

static inline int raise(int sig) {
    kill(getpid(), sig);
    return 0;
}


#ifndef SIGNAL_H
#define SIGNAL_H
typedef int sig_atomic_t;
#endif

#ifdef __cplusplus
}
#endif
