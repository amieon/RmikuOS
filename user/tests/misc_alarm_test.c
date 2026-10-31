/* alarm() / setitimer(ITIMER_REAL) 端到端测试
 *
 * 这个用例存在的理由是一次真实事故：alarm() 长期是个空桩（直接 return 0、
 * 不装闹钟），而内核其实早就实现了 ITIMER_REAL。后果不是"少个功能"，是
 * **所有靠 alarm 做超时的程序永不超时** —— openssl speed 用 alarm(seconds)
 * 控制每档跑几秒，到点由 handler 把 run 置 0 退出 `while (run)`；闹钟不响，
 * 一档永远结束不了，现场表现是"巨慢、卡住不动"。
 *
 * 所以这里**不能只测返回值**（空桩返回值也是 0，能过）：
 *   1. alarm() 的返回值：上次剩余秒数（不足 1 秒向上取整）、无未决闹钟返回 0。
 *   2. 信号必须真的到（等 handler 置位），这是核心断言。
 *   3. alarm(0) 必须真的取消：取消后信号不能再来。
 *   4. 重复装闹钟以最后一次为准。
 *
 * 所有等待都有**单调钟上界**：断言失败就 FAIL 退出，绝不靠"反正信号会来"
 * 把 CI 拖到超时 —— 这条规矩和 net_backlog_test 里避免 accept() 挂死是同一个道理。
 */
#include "test.h"
#include <signal.h>
#include <unistd.h>
#include <time.h>

static volatile int got_alarm;

static void on_alarm(int sig) {
    (void)sig;
    got_alarm = 1;
}

/* 等 got_alarm 置位，最多 ms 毫秒。返回 1 = 等到了，0 = 超时。
 * 用 usleep 而不是纯自旋：既省 CPU，也顺带验证"睡眠期间定时器照常推进"。 */
static int wait_alarm(int ms) {
    struct timespec t0, now;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        if (got_alarm) return 1;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = (now.tv_sec - t0.tv_sec) * 1000
                     + (now.tv_nsec - t0.tv_nsec) / 1000000;
        if (elapsed >= ms) return 0;
        usleep(20000);          /* 20ms 一次, 别把单核吃满 */
    }
}

int main(void) {
    /* 1) 装 handler。默认动作是"终止进程"，装不上就不能往下测 */
    void (*old)(int) = signal(SIGALRM, on_alarm);
    CHECK(old != SIG_ERR, "signal(SIGALRM) 注册成功");

    /* 2) 装闹钟的返回值：此刻没有未决闹钟 -> 0 */
    got_alarm = 0;
    CHECK_EQ(alarm(1), 0, "首次 alarm(1) 返回 0（此前无未决闹钟）");

    /* 3) 核心：信号必须真的到 */
    CHECK(wait_alarm(3000), "alarm(1) 之后 3 秒内收到 SIGALRM");

    /* 4) 重复装：以最后一次为准，且返回上一次的剩余秒数 */
    got_alarm = 0;
    alarm(30);                            /* 先装个长的 */
    unsigned int rem = alarm(1);           /* 立刻改短的 */
    CHECK(rem >= 29 && rem <= 30, "重装闹钟返回上一次剩余（约 30 秒）");
    CHECK(wait_alarm(3000), "重装为 1 秒后仍然按时到达");

    /* 5) alarm(0) 取消：不能再收到信号。
     *    等待时间必须**长于**被取消的那个闹钟，否则这条断言没有意义。 */
    got_alarm = 0;
    alarm(1);
    unsigned int r = alarm(0);
    CHECK(r >= 1, "alarm(0) 返回被取消闹钟的剩余秒数");
    CHECK(!wait_alarm(2000), "alarm(0) 取消后 2 秒内没有 SIGALRM");

    /* 6) 收尾：撤掉可能残留的闹钟与 handler */
    alarm(0);
    signal(SIGALRM, old);

    TEST_END();
}
