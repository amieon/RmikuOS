/*
 * io_poll_test.c —— poll() I/O 多路复用测试
 *
 * 覆盖:
 *   [1] 空管道 POLLIN 超时返回 0
 *   [2] 管道写入后读端报 POLLIN
 *   [3] 管道写端缓冲未满时报 POLLOUT
 *   [4] 写端全部关闭后,读端 EOF 也报 POLLIN
 *   [5] 读端全部关闭后,写端报 POLLERR
 *   [6] fd=-1 的 pollfd 被忽略;非法 fd 报 POLLNVAL
 *   [7] UDP socket 恒可写(POLLOUT),无数据时不报 POLLIN
 *   [8] UDP 发 DNS 查询后,poll 等到 POLLIN,再 recv 验证应答
 *
 * [1]--[7] 不依赖外网;[8] 依赖 DHCP 与 QEMU slirp 内建 DNS(10.0.2.3)。
 */
#include "test.h"

/* slirp 内建 DNS:10.0.2.3(主机序) */
#define DNS_SERVER_IP 0x0A000203u
#define DNS_PORT      53

static int build_dns_query(unsigned char *q) {
    int n = 0;
    q[n++] = 0x5e; q[n++] = 0xed;   /* ID */
    q[n++] = 0x01; q[n++] = 0x00;   /* RD=1 */
    q[n++] = 0; q[n++] = 1;         /* QDCOUNT */
    q[n++] = 0; q[n++] = 0;
    q[n++] = 0; q[n++] = 0;
    q[n++] = 0; q[n++] = 0;
    q[n++] = 7;
    { const char *s = "example"; while (*s) q[n++] = (unsigned char)*s++; }
    q[n++] = 3;
    { const char *s = "com"; while (*s) q[n++] = (unsigned char)*s++; }
    q[n++] = 0;
    q[n++] = 0; q[n++] = 1;         /* A */
    q[n++] = 0; q[n++] = 1;         /* IN */
    return n;
}

int main(void) {
    TEST_START("io_poll");

    /* ---- [1]--[4]: 管道读端 ---- */
    int p[2] = { -1, -1 };
    CHECK(pipe(p) == 0, "pipe create");
    if (p[0] >= 0 && p[1] >= 0) {
        struct pollfd in = { p[0], POLLIN, 0 };
        CHECK(poll(&in, 1, 20) == 0, "empty pipe poll timeout");
        CHECK(in.revents == 0, "empty pipe revents is zero");

        char ch = 'x';
        CHECK(write(p[1], &ch, 1) == 1, "pipe write one byte");
        in.revents = 0;
        CHECK(poll(&in, 1, 1000) == 1, "pipe readable after write");
        CHECK((in.revents & POLLIN) != 0, "pipe revents has POLLIN");

        char out = 0;
        CHECK(read(p[0], &out, 1) == 1 && out == 'x', "pipe read byte back");

        struct pollfd both[2] = {
            { p[0], POLLIN, 0 },
            { p[1], POLLOUT, 0 },
        };
        CHECK(poll(both, 2, 0) == 1, "empty read + writable write end");
        CHECK(both[0].revents == 0, "drained pipe not readable");
        CHECK((both[1].revents & POLLOUT) != 0, "pipe write end POLLOUT");

        close(p[1]);
        in.events = POLLIN;
        in.revents = 0;
        CHECK(poll(&in, 1, 0) == 1, "writer close makes read end ready");
        CHECK((in.revents & POLLIN) != 0, "read EOF reported as POLLIN");
        close(p[0]);
    }

    /* ---- [5]: 读端全部关闭后的写端错误 ---- */
    {
        int p2[2] = { -1, -1 };
        CHECK(pipe(p2) == 0, "pipe create for write error");
        if (p2[0] >= 0 && p2[1] >= 0) {
            close(p2[0]);
            struct pollfd out = { p2[1], POLLOUT, 0 };
            CHECK(poll(&out, 1, 0) == 1, "reader close makes write end ready");
            CHECK((out.revents & POLLERR) != 0, "write end reports POLLERR");
            close(p2[1]);
        }
    }

    /* ---- [6]: fd 忽略与非法 fd ---- */
    {
        struct pollfd ignored = { -1, POLLIN, 0 };
        CHECK(poll(&ignored, 1, 0) == 0, "negative fd ignored");
        CHECK(ignored.revents == 0, "negative fd revents cleared");

        struct pollfd bad = { 4095, POLLIN, 0 };
        CHECK(poll(&bad, 1, 0) == 1, "invalid fd counted ready");
        CHECK((bad.revents & POLLNVAL) != 0, "invalid fd reports POLLNVAL");
    }

    /* ---- [7]--[8]: UDP socket ---- */
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(fd >= 0, "socket udp");
    if (fd >= 0) {
        struct sockaddr_in local = addr_of(0, 0);
        CHECK(bind(fd, &local, sizeof local) == 0, "udp bind port 0");

        struct pollfd u = { fd, POLLIN | POLLOUT, 0 };
        CHECK(poll(&u, 1, 0) == 1, "udp socket writable immediately");
        CHECK((u.revents & POLLOUT) != 0, "udp revents has POLLOUT");
        CHECK((u.revents & POLLIN) == 0, "idle udp not readable");

        struct sockaddr_in srv = addr_of(DNS_SERVER_IP, DNS_PORT);
        CHECK(connect(fd, &srv, sizeof srv) == 0, "udp connect dns");
        unsigned char q[64];
        int qlen = build_dns_query(q);
        CHECK(send(fd, q, qlen, 0) == qlen, "send dns query");

        u.events = POLLIN;
        u.revents = 0;
        int n = poll(&u, 1, 5000);
        CHECK(n == 1, "poll waits for dns reply");
        CHECK((u.revents & POLLIN) != 0, "dns reply reports POLLIN");
        if (n == 1 && (u.revents & POLLIN) != 0) {
            unsigned char resp[512];
            int r = recv(fd, resp, (int)sizeof resp, 0);
            CHECK(r > 12, "recv dns reply after poll");
            if (r > 12) {
                CHECK(resp[0] == 0x5e && resp[1] == 0xed, "reply ID matches");
                CHECK(((resp[6] << 8) | resp[7]) > 0, "reply ANCOUNT > 0");
            }
        }
        net_close(fd);
    }

    TEST_END();
}
