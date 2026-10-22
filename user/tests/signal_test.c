/*
 * signal_test.c —— 信号处置表与 handler 投递（sigframe / trampoline / SIGRETURN）
 *
 * 覆盖:
 *   [1]  handler 被调用一次，参数就是触发它的信号号
 *   [2]  handler 返回后主流程继续（既不是 exit，也不是死循环）
 *   [3]  handler 里可以正常发系统调用（trap 在 handler 内也能进出）
 *   [4]  同一个 handler 可反复进入，计数精确（现场每次都恢复正确）
 *   [5]  被中断的定点局部变量 / 累加器不受影响
 *   [6]  被中断的浮点运算不受影响（trap 保存了 f0-f31 + fcsr）
 *   [7]  两个信号各自独立计数，互不干扰
 *   [8]  signal() 返回旧处置（改回 SIG_DFL 时能看到旧 handler 地址）
 *   [9]  SIGKILL / SIGSTOP 不可改
 *   [10] SIG_IGN 生效：被忽略的 SIGTERM 不再终止进程
 *   [11] 默认动作生效：未捕获的 SIGTERM 终止子进程（WIFSIGNALED/WTERMSIG）
 *
 * 为什么值得单独一个测试文件：这条链路跨了内核态（置 pending、压 sigframe、
 * 改写 TrapContext）、trap 出口、用户态汇编（trampoline）和 SIGRETURN 四段，
 * 任何一段错了都表现为"程序莫名其妙地崩/死循环"，用断言一条条钉住最省事。
 */
#include "test.h"
#include "signal.h"

static volatile int usr1_hits = 0;
static volatile int usr2_hits = 0;
static volatile int last_signo = -1;
static volatile int syscall_in_handler_ok = 0;

/* handler 是普通 C 函数：内核把 pc 指到它的入口，ra 指到 trampoline */
static void on_usr(int sig) {
    last_signo = sig;
    if (sig == SIGUSR1) usr1_hits++;
    else                usr2_hits++;

    /* [3] 在 handler 里发一次系统调用 —— 此时又进一次内核，出内核前
     * do_signal 会再被调用（此时 pending 已清，不会递归）。 */
    if (getpid() > 0) syscall_in_handler_ok = 1;

    /* 顺手把浮点寄存器搅一遍：验证内核在 trap 里真的保存了 f0-f31 */
    volatile double t = 3.25 * 7.5;
    (void)t;
}

/* [5] 每轮都打断一次，局部变量和累加器必须一字不差 */
static int locals_survive(void) {
    int a = 11, b = 31;
    long acc = 0;
    for (int i = 0; i < 5; i++) {
        acc += (long)a * b;
        raise(SIGUSR1);
    }
    if (a != 11 || b != 31) return -1;
    if (acc != 5L * 11 * 31) return -1;
    return 0;
}

/* [6] 浮点：中途被打断，结果必须还是 x * 1.5（2.0 -> 3.0，二进制精确） */
static double fp_survive(double x) {
    double y = x * 1.5 + 0.25;
    raise(SIGUSR1);
    return y - 0.25;
}

int main(void) {
    TEST_START("signal");

    /* ---------- [1][2][3] 基本投递 ---------- */
    signal(SIGUSR1, on_usr);
    raise(SIGUSR1);
    CHECK_EQ(usr1_hits, 1, "handler 被调用一次");
    CHECK_EQ(last_signo, SIGUSR1, "handler 收到的就是 SIGUSR1");
    CHECK_EQ(syscall_in_handler_ok, 1, "handler 里能正常发系统调用");

    /* ---------- [4] 反复进入 ---------- */
    usr1_hits = 0;
    for (int i = 0; i < 5; i++) raise(SIGUSR1);
    CHECK_EQ(usr1_hits, 5, "连发 5 次 -> handler 进入 5 次");

    /* ---------- [5] 定点现场 ---------- */
    usr1_hits = 0;
    CHECK_EQ(locals_survive(), 0, "被中断 5 次的局部变量与累加器完好");

    /* ---------- [6] 浮点现场 ---------- */
    CHECK(fp_survive(2.0) == 3.0, "被中断的浮点运算结果正确");

    /* ---------- [7] 两个信号互不干扰 ---------- */
    usr1_hits = 0;
    usr2_hits = 0;
    signal(SIGUSR2, on_usr);
    raise(SIGUSR1);
    raise(SIGUSR2);
    raise(SIGUSR1);
    CHECK_EQ(usr1_hits, 2, "SIGUSR1 计数 == 2");
    CHECK_EQ(usr2_hits, 1, "SIGUSR2 计数 == 1");
    CHECK_EQ(last_signo, SIGUSR1, "last_signo 是最近一次投递的信号");

    /* ---------- [8] 返回旧处置 ---------- */
    sighandler_t oldh = signal(SIGUSR2, SIG_DFL);
    CHECK(oldh == on_usr, "signal() 返回旧 handler 地址");
    CHECK(signal(SIGUSR2, SIG_IGN) == SIG_DFL, "再次修改时返回的旧处置是 SIG_DFL");

    /* ---------- [9] 不可改的两个 ---------- */
    CHECK_EQ(signal(SIGKILL, SIG_IGN), SIG_ERR, "SIGKILL 不可改");
    CHECK_EQ(signal(SIGSTOP, SIG_IGN), SIG_ERR, "SIGSTOP 不可改");

    /* ---------- [10] SIG_IGN ---------- */
    isize ip = fork();
    if (ip == 0) {
        signal(SIGTERM, SIG_IGN);
        kill(getpid(), SIGTERM);      /* 投递发生在本次 syscall 返回用户态时 */
        _exit(0);                     /* 真的忽略了才会走到这里 */
    }
    if (ip > 0) {
        int st = 0;
        waitpid(ip, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "SIG_IGN：SIGTERM 不再终止进程");
    }

    /* ---------- [11] 默认动作 ----------
     * 子进程不装 handler，SIGTERM 走默认动作 → 被杀死而不是正常退出。
     * 这条同时验证了 `128 + sig` 的退出码在等待方看是 WIFSIGNALED。 */
    isize kp = fork();
    if (kp == 0) {
        signal(SIGTERM, SIG_DFL);
        kill(getpid(), SIGTERM);
        _exit(1);                     /* 不该到这里 */
    }
    if (kp > 0) {
        int st = 0;
        waitpid(kp, &st, 0);
        CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM,
              "默认动作：SIGTERM 终止子进程（WIFSIGNALED / WTERMSIG）");
    }

    signal(SIGUSR1, SIG_DFL);
    signal(SIGUSR2, SIG_DFL);

    TEST_END();
}
