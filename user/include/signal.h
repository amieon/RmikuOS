#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "types.h"
#include "syscall.h"
#include "process.h"

#define SIGHUP  1
#define SIGINT  2
#define SIGQUIT 3
#define SIGILL  4
#define SIGABRT 6
#define SIGFPE  8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
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

/* ---- sigaction / sigprocmask（POSIX）----
 * 内核的信号模型比 POSIX 简单：只有 SIG_DFL / SIG_IGN 两种处置，
 * 没有信号屏蔽字，也没有 sigreturn 通道。于是：
 *   - SIG_DFL / SIG_IGN 真正能装（走 SYS_SIGNAL）；
 *   - 自定义 handler 返回 -1 + ENOSYS，**不假装成功**（否则调用方以为 Ctrl-C
 *     会走自己的清理逻辑，实际不会）；
 *   - sa_mask / sigprocmask 被接受但无效果，返回 0 —— 返回 -1 会让 git 的
 *     start_command() 直接 die_errno，而"屏蔽不了 SIGPIPE"不影响本地仓库操作；
 *   - 查询旧处置只能报 SIG_DFL（内核不回报历史值）。
 * 结构体里**没有** sa_sigaction：SA_SIGINFO 风格回调在这个内核里没有意义，
 * git 也不用。少一个匿名 union，TCC 那边更稳。 */
#define RK_NSIG 32

typedef struct {
    unsigned long bits[(RK_NSIG + 63) / 64];
} sigset_t;

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SA_RESTART 0x10000000

struct sigaction {
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);
};

static inline int sigemptyset(sigset_t *set) {
    if (!set) return -1;
    for (unsigned i = 0; i < (RK_NSIG + 63) / 64; i++) set->bits[i] = 0;
    return 0;
}

static inline int sigfillset(sigset_t *set) {
    if (!set) return -1;
    for (unsigned i = 0; i < (RK_NSIG + 63) / 64; i++) set->bits[i] = ~0UL;
    return 0;
}

static inline int sigaddset(sigset_t *set, int sig) {
    if (!set || sig < 1 || sig > RK_NSIG) return -1;
    set->bits[(sig - 1) / 64] |= 1UL << ((sig - 1) % 64);
    return 0;
}

static inline int sigdelset(sigset_t *set, int sig) {
    if (!set || sig < 1 || sig > RK_NSIG) return -1;
    set->bits[(sig - 1) / 64] &= ~(1UL << ((sig - 1) % 64));
    return 0;
}

static inline int sigismember(const sigset_t *set, int sig) {
    if (!set || sig < 1 || sig > RK_NSIG) return -1;
    return (set->bits[(sig - 1) / 64] >> ((sig - 1) % 64)) & 1UL;
}

static inline int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    if (old) {
        old->sa_handler = SIG_DFL;      /* 内核不回报历史处置 */
        sigemptyset(&old->sa_mask);
        old->sa_flags = 0;
        old->sa_restorer = (void (*)(void))0;
    }
    if (!act) return 0;

    if (act->sa_handler == SIG_DFL || act->sa_handler == SIG_IGN) {
        return (signal(sig, act->sa_handler) == SIG_ERR) ? -1 : 0;
    }
    errno = ENOSYS;                     /* 自定义 handler 装不上 */
    return -1;
}

/* 无信号屏蔽字：接受并忽略，返回 0（否则 git 的 start_command 会 die） */
static inline int sigprocmask(int how, const sigset_t *set, sigset_t *old) {
    (void)how; (void)set;
    if (old) sigemptyset(old);
    return 0;
}


#ifndef SIGNAL_H
#define SIGNAL_H
typedef int sig_atomic_t;
#endif

#ifdef __cplusplus
}
#endif
