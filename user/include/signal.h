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

static inline int raise(int sig) {
    kill(getpid(), sig);
    return 0;
}

/* ---- sigaction / sigprocmask（POSIX）----
 * 处置（SIG_DFL / SIG_IGN / 自定义 handler）三种都能装，handler 也能真的跑起来
 * （内核把现场压到用户栈，返回时经 trampoline 走 SYS_SIGRETURN 恢复）。
 * 仍然没有的两样：
 *   - 信号屏蔽字（sigprocmask 接受但无效果，返回 0 —— 返回 -1 会让 git 的
 *     start_command() 直接 die_errno，而"屏蔽不了 SIGPIPE"不影响本地仓库操作）；
 *   - handler 执行期间不会自动屏蔽自身信号，`sa_flags` 一律忽略。
 * 查询旧处置只能报 SIG_DFL（内核没有"只查不改"的入口）。
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
        old->sa_handler = SIG_DFL;      /* 内核没有"只查不改"的入口 */
        sigemptyset(&old->sa_mask);
        old->sa_flags = 0;
        old->sa_restorer = (void (*)(void))0;
    }
    if (!act) return 0;

    /* 三种处置都走 signal()：它会把 sa_handler 和 trampoline 一起交给内核 */
    return (signal(sig, act->sa_handler) == SIG_ERR) ? -1 : 0;
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
