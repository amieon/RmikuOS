/*
 * net_backlog_test.c —— listen backlog 手工联测
 *
 * 本程序 listen(8080, backlog=1),先故意 5 秒不 accept。
 * 请在看到 READY 后,在宿主机立刻执行:
 *
 *   (nc -vz 127.0.0.1 8080; nc -vz -w 25 127.0.0.1 8080)
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
    CHECK(bind(lfd, &local, sizeof local) == 0, "bind 8080");
    CHECK(listen(lfd, 1) == 0, "listen backlog=1");

    printf("[INFO] READY: run on host now:\n");
    printf("[INFO]   (nc -vz 127.0.0.1 8080; nc -vz -w 25 127.0.0.1 8080)\n");
    printf("[INFO] kernel should log: backlog full ... drop SYN\n");
    fflush(stdout);

    /* 先不 accept,让第一个已完成连接占满唯一 backlog 名额。 */
    wait_us(5 * 1000 * 1000);

    struct pollfd p = { lfd, POLLIN, 0 };
    CHECK(poll(&p, 1, 20000) == 1, "first queued connection readable");
    CHECK((p.revents & POLLIN) != 0, "first connection POLLIN");

    struct sockaddr_in peer1;
    int c1 = accept(lfd, &peer1, 0);
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
    int c2 = accept(lfd, &peer2, 0);
    CHECK(c2 >= 0, "accept second connection after retry");
    if (c2 >= 0) {
        net_close(c2);
    }

    net_close(lfd);
    TEST_END();
}
