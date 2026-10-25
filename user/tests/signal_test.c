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
 *   [12] 同步异常被忽略时终止进程而不是打死内核
 *   [13] sigprocmask 屏蔽字（线程级）与 handler 期间自动屏蔽自身
 *   [14] 屏蔽字是线程级的（新线程继承、worker 解除不影响 main）
 *   [15] 同步异常走线程级 pending（handler 在出错线程上跑）
 *   [16] tgkill / pthread_kill 定向投递
 *   [17] sigpending 只报"被屏蔽且挂着"的
 *   [18] errno 通道 + EINTR（阻塞 read 被信号打断）
 *   [19] siglongjmp：从 handler 跳出去且恢复屏蔽字
 *   [20] SIGSEGV + siglongjmp（JIT trap）
 *   [21] WCOREDUMP：Core 类置 bit7、Term 类不置
 *   [22] sigaction 查询（act == NULL）
 *   [23] 停止/继续：WUNTRACED / WCONTINUED / 打死停止态进程
 *
 * 为什么值得单独一个测试文件：这条链路跨了内核态（置 pending、压 sigframe、
 * 改写 TrapContext）、trap 出口、用户态汇编（trampoline）和 SIGRETURN 四段，
 * 任何一段错了都表现为"程序莫名其妙地崩/死循环"，用断言一条条钉住最省事。
 */
#include "test.h"
#include "signal.h"
#include "errno.h"
#include "setjmp.h"

static volatile int usr1_hits = 0;
static volatile int usr2_hits = 0;
static volatile int last_signo = -1;
static volatile int syscall_in_handler_ok = 0;

/* handler 是普通 C 函数：内核把 pc 指到它的入口，ra 指到 trampoline */
/* 用来验证"handler 期间自动屏蔽自身"：在 handler 里再 raise 一次自己 */
static volatile int self_entries = 0, self_depth = 0, self_max = 0, self_once = 0;

static void on_self(int sig) {
    self_entries++;
    self_depth++;
    if (self_depth > self_max) self_max = self_depth;
    if (!self_once) { self_once = 1; raise(sig); }   /* 只试探一次 */
    self_depth--;
}

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

/* [14] 屏蔽字是线程级的：worker 继承、且解除后不影响 main */
static volatile int worker_inherited = 0, worker_got = 0, worker_done = 0;

static void mask_worker(void *arg) {
    (void)arg;
    sigset_t cur;
    sigprocmask(SIG_BLOCK, 0, &cur);              /* 查询本线程 */
    worker_inherited = sigismember(&cur, SIGUSR1);

    sigset_t b;
    sigemptyset(&b);
    sigaddset(&b, SIGUSR1);
    sigprocmask(SIG_UNBLOCK, &b, 0);              /* 只解除本线程 */
    usr1_hits = 0;
    raise(SIGUSR1);
    worker_got = (usr1_hits == 1);
    worker_done = 1;
}

/* [15] 同步异常走线程级 pending：worker 里空指针，handler 必须在该线程跑起来 */
static void on_segv(int sig) {
    (void)sig;
    /* 同步异常 handler 不能 return —— 回去就会重跑那条出错指令，无限循环。
     * 这里直接退出进程，让父进程用退出码判断"handler 确实跑了"。 */
    _exit(0);
}

static void segv_worker(void *arg) {
    (void)arg;
    *(volatile int *)0 = 1;      /* 空指针写：触发 SIGSEGV（同步异常） */
    _exit(2);                    /* 不该到这里 */
}

/* [16] 线程定向投递：pthread_kill 只打到指定线程 */
static volatile int tk_tid = -1, tk_ready = 0, tk_stop = 0;
static volatile int tk_handler_tid = -1, tk_hits = 0;

static void on_usr2_tid(int sig) {
    (void)sig;
    tk_handler_tid = gettid();        /* 记下"我是在哪个线程上跑的" */
    tk_hits++;
}

static void tk_worker(void *arg) {
    (void)arg;
    tk_tid = gettid();
    tk_ready = 1;
    while (!tk_stop) { }              /* 一直跑着，等信号在 trap 出口投递 */
}

/* [18] EINTR：main 阻塞在管道 read 上，被信号打断应返回 -1 + EINTR */
static volatile int eintr_main_tid = -1;
static int eintr_wfd = -1;

static void eintr_killer(void *arg) {
    (void)arg;
    for (volatile long i = 0; i < 20000000L; i++) { }   /* 等 main 睡进 read */
    pthread_kill(eintr_main_tid, SIGUSR1);              /* 定向打给 main */
    /* 兜底：万一没打断成功，写一个字节让 main 不至于永久阻塞 */
    for (volatile long i = 0; i < 20000000L; i++) { }
    if (eintr_wfd >= 0) write(eintr_wfd, "x", 1);
}

/* [19] siglongjmp：从 handler 里跳出去，且屏蔽字要被恢复 */
static sigjmp_buf g_jmp;
static volatile int g_jmp_hits = 0;
static volatile int g_jmp_should_jump = 1;

static void on_usr1_jump(int sig) {
    (void)sig;
    g_jmp_hits++;
    if (g_jmp_should_jump) siglongjmp(g_jmp, 42);
}

/* [20] SIGSEGV handler 里 siglongjmp：跳过出错指令继续跑（JIT trap 的基础） */
static sigjmp_buf g_segv_jmp;
static void on_segv_jump(int sig) { (void)sig; siglongjmp(g_segv_jmp, 7); }

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

    /* ---------- [12] 屏蔽字（sigprocmask） ---------- */
    sigset_t blk, prev, cur;
    sigemptyset(&blk);
    sigaddset(&blk, SIGUSR1);

    CHECK_EQ(sigprocmask(SIG_BLOCK, &blk, &prev), 0, "sigprocmask(SIG_BLOCK) 成功");
    CHECK(sigismember(&prev, SIGUSR1) == 0, "屏蔽前 SIGUSR1 不在屏蔽字里");

    usr1_hits = 0;
    raise(SIGUSR1);
    CHECK_EQ(usr1_hits, 0, "屏蔽期间不投递（信号挂在 pending 上）");

    CHECK_EQ(sigprocmask(SIG_UNBLOCK, &blk, &cur), 0, "sigprocmask(SIG_UNBLOCK) 成功");
    CHECK(sigismember(&cur, SIGUSR1) == 1, "解除前屏蔽字里确实有 SIGUSR1");
    CHECK_EQ(usr1_hits, 1, "解除屏蔽后补投——屏蔽期间那个信号没丢");

    /* SIGKILL 不可屏蔽：置不上也不报错 */
    sigset_t kb;
    sigemptyset(&kb);
    sigaddset(&kb, SIGKILL);
    sigprocmask(SIG_BLOCK, &kb, 0);
    sigprocmask(SIG_BLOCK, 0, &cur);
    CHECK(sigismember(&cur, SIGKILL) == 0, "SIGKILL 无法被屏蔽");

    /* ---------- [13] handler 期间自动屏蔽自身 ----------
     * handler 里 raise 自己：若没有自动屏蔽，会立刻递归（self_max 变成 2+）。
     * 正确行为是挂到 pending，等 sigreturn 恢复屏蔽字后补投一次。 */
    signal(SIGUSR2, on_self);
    self_entries = self_depth = self_max = self_once = 0;
    raise(SIGUSR2);
    CHECK_EQ(self_max, 1, "handler 里 raise 自己不会递归（期间已屏蔽自身）");
    CHECK_EQ(self_entries, 2, "挂起的那次在 sigreturn 后补投（总共进入 2 次）");

    /* ---------- [14] 屏蔽字是线程级的 ---------- */
    sigset_t mb;
    sigemptyset(&mb);
    sigaddset(&mb, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mb, 0);               /* main 先屏蔽，worker 会继承 */

    int wtid = thread_create(mask_worker, 0);
    CHECK(wtid >= 0, "thread_create 成功（用于验证线程级屏蔽字）");
    if (wtid >= 0) {
        /* 自己转着等，不直接 join：kill 会把同进程的线程都唤成 Ready，
         * 正在 join 里阻塞的 main 可能被提前唤醒，导致断言读到半成品状态。
         * 有界自旋 —— worker 卡住时报失败而不是把测试挂死。 */
        for (long spin = 0; spin < 50000000L && !worker_done; spin++) { }
        CHECK_EQ(worker_done, 1, "worker 跑完了（有界自旋）");
        int wcode = 0;
        thread_join(wtid, &wcode);
        CHECK_EQ(worker_inherited, 1, "新线程继承了创建者的屏蔽字");
        CHECK_EQ(worker_got, 1, "worker 解除屏蔽后自己收到了信号");
        sigset_t after;
        sigprocmask(SIG_BLOCK, 0, &after);
        CHECK(sigismember(&after, SIGUSR1) == 1, "worker 的解除不影响 main（屏蔽字确实是线程级）");
    }
    sigprocmask(SIG_UNBLOCK, &mb, 0);             /* 收尾 */

    /* ---------- [15] 同步异常（线程级 pending） ----------
     * 在**子进程的 worker 线程**里故意空指针：SIGSEGV 必须立刻在该线程投递，
     * handler 里 _exit(0)。若投递错到主线程、或根本没投递，进程会以 139 退出。 */
    isize sp = fork();
    if (sp == 0) {
        signal(SIGSEGV, on_segv);
        if (thread_create(segv_worker, 0) < 0) _exit(3);
        for (;;) { }                  /* 主线程空转，让 worker 去触发 */
    }
    if (sp > 0) {
        int sst2 = 0;
        waitpid(sp, &sst2, 0);
        CHECK(WIFEXITED(sst2) && WEXITSTATUS(sst2) == 0,
              "SIGSEGV handler 在出错线程上跑起来了（同步信号走线程级 pending）");
    }

    /* ---------- [16] 线程定向投递（pthread_kill / tgkill） ---------- */
    signal(SIGUSR2, on_usr2_tid);
    int ktid = thread_create(tk_worker, 0);
    CHECK(ktid >= 0, "thread_create 成功（pthread_kill 测试）");
    if (ktid >= 0) {
        for (long s = 0; s < 50000000L && !tk_ready; s++) { }
        CHECK_EQ(tk_tid, ktid, "worker 自报的 tid 与 thread_create 返回值一致");

        CHECK_EQ(pthread_kill(ktid, SIGUSR2), 0, "pthread_kill(worker, SIGUSR2) 成功");
        for (long s = 0; s < 200000000L && tk_hits == 0; s++) { }
        CHECK_EQ(tk_hits, 1, "信号确实被投递了");
        CHECK_EQ(tk_handler_tid, ktid, "handler 在指定线程上跑（main 没有代收）");

        tk_stop = 1;
        int kcode = 0;
        thread_join(ktid, &kcode);
    }

    /* ---------- [17] sigpending：只报"被屏蔽且挂着"的 ---------- */
    sigset_t pb, pend;
    sigemptyset(&pb);
    sigaddset(&pb, SIGUSR1);
    sigprocmask(SIG_BLOCK, &pb, 0);
    usr1_hits = 0;
    raise(SIGUSR1);

    CHECK_EQ(sigpending(&pend), 0, "sigpending 成功");
    CHECK(sigismember(&pend, SIGUSR1) == 1, "被屏蔽且挂起的 SIGUSR1 报出来了");
    CHECK(sigismember(&pend, SIGTERM) == 0, "没挂起的信号不在里面");

    sigprocmask(SIG_UNBLOCK, &pb, 0);
    CHECK_EQ(usr1_hits, 1, "解除屏蔽后那个信号补投了（和 sigpending 看到的一致）");

    /* ---------- [18] errno 通道 + EINTR ---------- */
    char ebuf[4];
    errno = 0;
    CHECK_EQ(read(-1, ebuf, sizeof ebuf), -1, "read(非法 fd) 返回 -1");
    CHECK_EQ(errno, EBADF, "errno == EBADF（不再是笼统的 -1）");

    int pf[2];
    isize pr = pipe(pf);
    CHECK_EQ(pr, 0, "pipe() 成功（EINTR 测试）");
    if (pr == 0) {
        eintr_wfd = pf[1];
        eintr_main_tid = gettid();
        signal(SIGUSR1, on_usr);
        usr1_hits = 0;

        int etid = thread_create(eintr_killer, 0);
        if (etid >= 0) {
            errno = 0;
            isize en = read(pf[0], ebuf, sizeof ebuf);
            CHECK_EQ(en, -1, "阻塞的 read 被信号打断，返回 -1");
            CHECK_EQ(errno, EINTR, "errno == EINTR（而不是继续睡到数据到达）");
            CHECK_EQ(usr1_hits, 1, "打断之后 handler 也跑过了");
            int ecode = 0;
            thread_join(etid, &ecode);
        }
        close(pf[0]);
        close(pf[1]);
    }

    /* ---------- [19] siglongjmp ---------- */
    signal(SIGUSR1, on_usr1_jump);
    g_jmp_hits = 0;
    g_jmp_should_jump = 1;
    int jv;
    jv = sigsetjmp(g_jmp, 1);
    if (jv == 0) {
        raise(SIGUSR1);            /* 进 handler → 里面 siglongjmp 跳回这里 */
    }
    CHECK_EQ(jv, 42, "siglongjmp 把 42 带回了 sigsetjmp 的返回点");

    g_jmp_should_jump = 0;         /* 这一次让 handler 正常返回 */
    raise(SIGUSR1);
    CHECK_EQ(g_jmp_hits, 2, "跳出去之后 SIGUSR1 仍能再投递（屏蔽字已被恢复）");

    /* ---------- [20] SIGSEGV + siglongjmp（JIT 的 trap 机制） ---------- */
    signal(SIGSEGV, on_segv_jump);
    int sv;
    sv = sigsetjmp(g_segv_jmp, 1);
    if (sv == 0) {
        *(volatile int *)0 = 1;    /* 故意炸；handler 里跳回来就等于"跳过这条指令" */
    }
    CHECK_EQ(sv, 7, "SIGSEGV handler 里 siglongjmp 能跳出来");

    /* ---------- [21] WCOREDUMP：Core 类与 Term 类的区别 ----------
     * 两者都"被信号杀死"，唯一的差别是 wait status 的 bit7。
     * 这一组必须成对看：只有 SIGSEGV 置位、SIGTERM 不置位，才说明
     * 这个位是按信号分类算出来的，不是恒置/恒不置。 */
    isize dp = fork();
    if (dp == 0) {
        signal(SIGSEGV, SIG_DFL);
        *(volatile int *)0 = 1;          /* 默认动作 = Core */
        _exit(0);
    }
    if (dp > 0) {
        int dst = 0;
        waitpid(dp, &dst, 0);
        CHECK(WIFSIGNALED(dst) && WTERMSIG(dst) == SIGSEGV,
              "未捕获的 SIGSEGV 杀死进程");
        CHECK(WCOREDUMP(dst), "SIGSEGV 是 Core 类 -> WCOREDUMP 为真");
    }

    isize tp = fork();
    if (tp == 0) {
        signal(SIGTERM, SIG_DFL);
        raise(SIGTERM);                  /* 默认动作 = Term */
        _exit(0);
    }
    if (tp > 0) {
        int tst = 0;
        waitpid(tp, &tst, 0);
        CHECK(WIFSIGNALED(tst) && WTERMSIG(tst) == SIGTERM,
              "未捕获的 SIGTERM 杀死进程");
        CHECK(!WCOREDUMP(tst), "SIGTERM 是 Term 类 -> 没有 core 位");
    }

    /* ---------- [22] sigaction 查询（act == NULL） ----------
     * 老实现一律报 SIG_DFL —— 装了 handler 的程序自己回读会发现"没装上"。 */
    struct sigaction qa, qo;
    qa.sa_handler = on_usr1_jump;
    sigemptyset(&qa.sa_mask);
    qa.sa_flags = 0;
    qa.sa_restorer = (void (*)(void))0;

    CHECK_EQ(sigaction(SIGUSR2, &qa, 0), 0, "sigaction 装 handler 成功");
    CHECK_EQ(sigaction(SIGUSR2, 0, &qo), 0, "sigaction(sig, NULL, &old) 查询成功");
    CHECK(qo.sa_handler == on_usr1_jump, "查询回来的就是装进去的 handler 地址");
    CHECK(qo.sa_restorer != (void (*)(void))0, "handler 的 sa_restorer 非空（trampoline）");

    signal(SIGUSR2, SIG_IGN);
    sigaction(SIGUSR2, 0, &qo);
    CHECK(qo.sa_handler == SIG_IGN, "SIG_IGN 能被查询出来");
    CHECK(qo.sa_restorer == (void (*)(void))0, "SIG_IGN 没有 restorer");

    signal(SIGUSR2, SIG_DFL);
    sigaction(SIGUSR2, 0, &qo);
    CHECK(qo.sa_handler == SIG_DFL, "恢复 SIG_DFL 后查询得到 SIG_DFL");

    /* 查询 SIGKILL 的处置是合法的（POSIX 允许），只是改不了 */
    CHECK_EQ(sigaction(SIGKILL, 0, &qo), 0, "查询 SIGKILL 的处置允许（改不了但能看）");
    CHECK(qo.sa_handler == SIG_DFL, "SIGKILL 的处置恒为 SIG_DFL");

    /* ---------- [23] 停止 / 继续（作业控制的内核侧） ----------
     * 链路：kill(SIGSTOP) -> 目标在 trap 出口停下并切走 CPU
     *      -> 父进程 waitpid(WUNTRACED) 拿到 WIFSTOPPED
     *      -> kill(SIGCONT) 重新入队 -> waitpid(WCONTINUED) 拿到继续事件。
     * 没有任何一环时，Ctrl+Z/fg/bg 都不可能成立。 */
    isize zp = fork();
    if (zp == 0) {
        for (;;) { }                 /* 空转：只在定时器中断的出口检查信号 */
    }
    if (zp > 0) {
        for (volatile long i = 0; i < 2000000L; i++) { }   /* 让子进程先跑起来 */

        CHECK_EQ(kill(zp, SIGSTOP), 0, "SIGSTOP 投递成功");
        int sst = 0;
        CHECK_EQ(waitpid(zp, &sst, WUNTRACED), zp, "waitpid(WUNTRACED) 报出被停住的孩子");
        CHECK(WIFSTOPPED(sst), "WIFSTOPPED 成立");
        CHECK_EQ(WSTOPSIG(sst), SIGSTOP, "WSTOPSIG == SIGSTOP");

        /* 同一次停止只报一次：否则 shell 每轮 reap_jobs 都会打印一行 */
        int sst2 = 0;
        CHECK_EQ(waitpid(zp, &sst2, WUNTRACED | WNOHANG), 0, "同一次停止不重复上报");

        /* 不带 WUNTRACED 时不该看到停止事件（这是 waitpid 的语义边界） */
        int sst3 = 0;
        CHECK_EQ(waitpid(zp, &sst3, WNOHANG), 0, "不带 WUNTRACED 时 waitpid 不报停止");

        CHECK_EQ(kill(zp, SIGCONT), 0, "SIGCONT 投递成功");
        int cst = 0;
        CHECK_EQ(waitpid(zp, &cst, WCONTINUED), zp, "waitpid(WCONTINUED) 报出继续事件");
        CHECK(WIFCONTINUED(cst), "WIFCONTINUED 成立");

        int alive = 0;
        CHECK_EQ(waitpid(zp, &alive, WNOHANG | WUNTRACED), 0,
                 "继续之后既没退出、也没再停（真的在跑）");

        /* 再停一次，然后直接 SIGKILL —— 走"打死停止态进程"那条特殊路径
         * （普通 kill 只置 pending，而停止态永远跑不到 trap 出口）。 */
        kill(zp, SIGSTOP);
        int st4 = 0;
        waitpid(zp, &st4, WUNTRACED);
        kill(zp, SIGKILL);
        int st5 = 0;
        CHECK_EQ(waitpid(zp, &st5, 0), zp, "停住的进程能被 SIGKILL 收掉");
        CHECK(WIFSIGNALED(st5) && WTERMSIG(st5) == SIGKILL, "退出原因是 SIGKILL");
        CHECK(!WCOREDUMP(st5), "SIGKILL 不置 core 位");
    }

    signal(SIGUSR1, SIG_DFL);
    signal(SIGUSR2, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);

    TEST_END();
}
