#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "types.h"
#include "syscall.h"
#include "process.h"
#include "errno.h"

/* 信号编号与 Linux 一致：
 *   1..31   标准信号（Linux 上这 31 个号**已经排满**，新信号不会往这里插）
 *   32..64  实时信号段（Linux 里 32/33 归 glibc，实际可用 34..64）。
 *           本内核未实现（不排队、无 sigqueue），但位图按 64 位留好了位置。
 */
#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28   /* 终端是固定 80x24，内核不产生它；能装 handler，只是永不触发 */
#define SIGIO     29
#define SIGPWR    30
#define SIGSYS    31

typedef void (*sighandler_t)(int);

#define SIG_DFL   ((sighandler_t)0)
#define SIG_IGN   ((sighandler_t)1)
#define SIG_ERR   ((sighandler_t)-1)

static inline void kill(isize pid, usize sig){
    syscall3(SYS_KILL, pid, sig, 0);
}

/* handler 返回时要去的地方 —— 由 lib/syscall_*.S 里的裸汇编提供：
 * 把 SYS_SIGRETURN 放进 a7 然后 ecall，两三条指令，不改 sp。
 *
 * 为什么必须要它：内核投递 handler 时把 ra 指向这里。handler 是个普通 C
 * 函数，它的 `ret` 会跳到 ra —— 如果 ra 是调用者的返回地址，恢复现场就
 * 无从谈起；如果 ra 是 0，直接就崩。所以这个"假返回地址"是整套机制的地基。 */
extern void rk_sigreturn_trampoline(void);

/* signal(): SIG_DFL / SIG_IGN / 自定义 handler 三种都能装。
 *   返回旧处置（SIG_DFL / SIG_IGN / 旧 handler 地址），失败 SIG_ERR。
 *
 * 装 handler 时把 trampoline 一起传给内核（第三个参数），因为内核在投递时
 * 需要知道"handler 返回后去哪"——用户态地址只有用户态知道。 */
static inline sighandler_t signal(int sig, sighandler_t handler) {
    isize old = syscall3(SYS_SIGNAL, (usize)sig, (usize)handler,
                         (usize)&rk_sigreturn_trampoline);
    if (old < 0) return SIG_ERR;
    return (sighandler_t)old;
}

static inline int pthread_kill(int tid, int sig) {
    isize r = syscall3(SYS_TGKILL, (usize)getpid(), (usize)tid, (usize)sig);
    return (r < 0) ? -1 : 0;
}

static inline int raise(int sig) {
    kill(getpid(), sig);
    return 0;
}


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

#define RK_SIG_QUERY   ((usize)-1)

/* sigaction：act 为 NULL 时只查询旧处置（`sigaction(sig, NULL, &old)`），
 * 这是 POSIX 里唯一能读到"当前装了什么"的途径 —— 之前老处置一律报 SIG_DFL，
 * 装了 handler 的程序自己回读会发现"我明明装了"，属于会误导人的错答案。 */
static inline int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
    if (!act) {
        if (!old) return 0;                      /* 既不改也不查：合法空操作 */
        isize prev = syscall3(SYS_SIGNAL, (usize)sig, RK_SIG_QUERY, 0);
        if (prev < 0) { errno = EINVAL; return -1; }
        old->sa_handler = (prev == 0) ? SIG_DFL
                        : (prev == 1) ? SIG_IGN
                                      : (sighandler_t)prev;
        sigemptyset(&old->sa_mask);
        old->sa_flags = 0;                       /* sa_flags 内核不接受，恒 0 */
        old->sa_restorer = (prev > 1) ? (void (*)(void))&rk_sigreturn_trampoline
                                      : (void (*)(void))0;
        return 0;
    }

    /* (sighandler_t)-1 恰好等于内核的"只查不改"哨兵，必须挡掉：
     * SIG_ERR 本来就不是合法 handler，放进去会变成"静默什么都不做"。 */
    if (act->sa_handler == SIG_ERR) { errno = EINVAL; return -1; }

    /* 三种处置都走 signal()：它会把 sa_handler 和 trampoline 一起交给内核 */
    if (signal(sig, act->sa_handler) == SIG_ERR) {
        errno = EINVAL;                          /* 号越界 / SIGKILL / SIGSTOP */
        return -1;
    }
    return 0;
}

/* 无信号屏蔽字：接受并忽略，返回 0（否则 git 的 start_command 会 die） */
/* sigprocmask: how = SIG_BLOCK/SIG_UNBLOCK/SIG_SETMASK，set 为 0 表示只查询。
 * **作用于当前线程**（POSIX 里屏蔽字是线程级的）；sigset_t 正好 8 字节，内核按 u64 读写。 */
static inline int sigprocmask(int how, const sigset_t *set, sigset_t *old) {
    isize r = syscall3(SYS_SIGPROCMASK, (usize)how, (usize)set, (usize)old);
    return (r < 0) ? -1 : 0;
}

static inline int sigpending(sigset_t *set) {
    isize r = syscall3(SYS_SIGPENDING, (usize)set, 0, 0);
    return (r < 0) ? -1 : 0;
}

/* POSIX: 多线程程序里 sigprocmask 是未定义行为，应改用这个 ——
 * 语义完全一样（都作用于调用线程），只是名字说明了作用范围。 */
static inline int pthread_sigmask(int how, const sigset_t *set, sigset_t *old) {
    return sigprocmask(how, set, old);
}


#ifndef SIGNAL_H
#define SIGNAL_H
typedef int sig_atomic_t;
#endif

#ifdef __cplusplus
}
#endif
