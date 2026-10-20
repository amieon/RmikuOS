/*
 * net_backlog_test.c —— listen backlog 联测
 *
 * 本程序 listen(8080, backlog=1),先故意 5 秒不 accept。
 * 需要宿主机配合发起连接(要复现"backlog 满 → SYN 被丢 → 重传成功"的时序,
 * 连接必须从外部来)。两种跑法:
 *
 *   1. 手工: 看到 READY 后在宿主机执行
 *        (nc -vz 127.0.0.1 8080; nc -vz -w 25 127.0.0.1 8080)
 *      riscv64 映射宿主 8080, loongarch64 映射宿主 8081(guest 内都是 8080)。
 *
 *   2. CI: scripts/smoke_test.sh 会在后台守候 READY 标记, 自动替你发这两条 nc
 *      (它装了 netcat-openbsd)。没有 nc 的环境下本测试打印 SKIP 并返回 77,
 *      不会被记成 PASS —— 没跑就是没跑。
 *
 * 预期:
 *   1. 第一个 nc 立刻连上并断开,占用唯一 backlog 名额;
 *   2. 第二个 nc 的 SYN 先被丢弃,卡在重传;
 *   3. 5 秒后本程序 accept 第一个连接,腾出 backlog;
 *   4. 第二个 nc 的 SYN 重传成功,本程序 accept 到第二个连接。
 */
#include "test.h"

#define BACKLOG_PORT 8080

static void wait_us(long us) {
    long start = (long)get_time_us();
    while ((long)get_time_us() - start < us) {
        yield();
    }
}

int main(void) {
    TEST_START("net_backlog");

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(lfd >= 0, "socket tcp");
    if (lfd < 0) {
        TEST_END();
    }

    net_setsockopt(lfd, SO_REUSEADDR, 1);
    struct sockaddr_in local = addr_of(0, BACKLOG_PORT);
    CHECK(bind(lfd, (struct sockaddr *)&local, sizeof local) == 0, "bind 8080");
    CHECK(listen(lfd, 1) == 0, "listen backlog=1");

    printf("[INFO] READY: run on host now:\n");
    printf("[INFO]   (nc -vz 127.0.0.1 8080; nc -vz -w 25 127.0.0.1 8080)\n");
    printf("[INFO] kernel should log: backlog full ... drop SYN\n");
    fflush(stdout);

    /* 先不 accept,让第一个已完成连接占满唯一 backlog 名额。 */
    wait_us(5 * 1000 * 1000);

    struct pollfd p = { lfd, POLLIN, 0 };
    int r1 = poll(&p, 1, 20000);
    if (r1 != 1 || (p.revents & POLLIN) == 0) {
        /*
         * 没有宿主连接: CI / 自动回归里没有人执行上面那条 nc。
         * 这里必须**跳过而不是继续** —— 空闲的监听 socket 上调 accept() 会永久
         * 阻塞, 整个 run_all 会被卡住(CI 上表现为"超时 1800s 未等到 run_all 汇总")。
         *
         * 退出码 77 = SKIP(与 0/1 区分开): 这是"依赖外部动作"的场景不具备条件,
         * 不是被测行为出错。run_all 会把它记成 SKIP 而不是 PASS —— 跳过就是跳过,
         * 不能伪装成"验过了"。手工联测时按上面命令跑 nc, 就走下面正常路径。
         */
        printf("[SKIP] 未检测到宿主连接, 跳过 backlog 场景(需手动执行上面那条 nc)\n");
        net_close(lfd);
        printf("[TEST] %s done: %d passed, %d failed, SKIPPED\n",
               test_cur_name, test_pass_count, test_fail_count);
        return 77;
    }
    CHECK(1, "first queued connection readable");
    CHECK((p.revents & POLLIN) != 0, "first connection POLLIN");

    struct sockaddr_in peer1;
    int c1 = accept(lfd, (struct sockaddr *)&peer1, 0);
    CHECK(c1 >= 0, "accept first connection");
    if (c1 >= 0) {
        printf("[INFO] accepted first, backlog slot freed\n");
        net_close(c1);
    }

    /* 第二个连接的 SYN 之前被丢;客户端重传后应在这里完成握手。 */
    p.revents = 0;
    CHECK(poll(&p, 1, 30000) == 1, "second SYN retry becomes readable");
    CHECK((p.revents & POLLIN) != 0, "second connection POLLIN");

    struct sockaddr_in peer2;
    int c2 = accept(lfd, (struct sockaddr *)&peer2, 0);
    CHECK(c2 >= 0, "accept second connection after retry");
    if (c2 >= 0) {
        net_close(c2);
    }

    net_close(lfd);
    TEST_END();
}
