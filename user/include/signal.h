#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "types.h"
#include "syscall.h"

#define SIGINT  2
#define SIGILL  4
#define SIGABRT 6
#define SIGFPE  8
#define SIGKILL 9
#define SIGTERM 15

static inline void kill(isize pid, usize sig){
    syscall3(SYS_KILL, pid, sig, 0);
}

#ifndef SIGNAL_H
#define SIGNAL_H
typedef int sig_atomic_t;
#endif

#ifdef __cplusplus
}
#endif
