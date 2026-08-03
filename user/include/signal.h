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
#define SIGSEGV 11
#define SIGTERM 15
#define SIGCHLD 17
/* SIGWINCH: 窗口变化信号——RmikuOS 内核不产生(终端固定 80x24)。
 * 定义仅为编译兼容(kilo 等); signal(SIGWINCH, ...) 会返回 SIG_ERR,
 * 调用方不检查则等价于"不装 handler", 因信号永不触发而无害。 */
#define SIGWINCH 28

typedef void (*sighandler_t)(int);

#define SIG_DFL   ((sighandler_t)0)
#define SIG_IGN   ((sighandler_t)1)
#define SIG_ERR   ((sighandler_t)-1)

static inline void kill(isize pid, usize sig){
    syscall3(SYS_KILL, pid, sig, 0);
}

/* 真实 signal(): 内核支持 SIG_DFL(0)/SIG_IGN(1) 两种处置;
 * 自定义 handler 不支持, 返回 SIG_ERR。返回旧处置(SIG_DFL/SIG_IGN)。 */
static inline sighandler_t signal(int sig, sighandler_t handler) {
    isize old = syscall3(SYS_SIGNAL, (usize)sig, (usize)handler, 0);
    if (old < 0) return SIG_ERR;
    return (sighandler_t)old;
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
