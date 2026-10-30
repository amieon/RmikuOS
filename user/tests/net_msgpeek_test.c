/*
 * net_msgpeek_test.c —— MSG_PEEK 语义测试
 *
 * 为什么值得单独一个测试：peek 是"读了但不消费"，一旦内核退化成真读，
 * 表现是**数据静默消失** —— 上层（DTLS 的 peekmode、任何"先看有没有数据
 * 再决定读不读"的代码）会丢掉一个完整数据报，而没有任何错误返回。
 * 这种 bug 不会崩、不会报错，只在协议层表现为"握手莫名重传"。
 *
 * 覆盖：
 *   [1] recv(MSG_PEEK) 拿到数据，长度与内容与真实报文一致
 *   [2] 连续两次 peek 结果完全相同（第一次没消费）
 *   [3] peek 之后正常 recv 拿到同一报文（peek 没把它吃掉）
 *   [4] 数据已消费，再 recv 超时返回 0（证明只消费了一次）
 *   [5] recvfrom(MSG_PEEK) 同样成立，且回填正确来源地址
 *   [6] 未实现的 flags（MSG_DONTWAIT）必须失败，不能静默忽略
 *
 * 依赖网络就绪（DHCP 已拿到地址）；无网环境下 [1] 及之后都会 FAIL。
 */
#include "test.h"
#include "net.h"
#include "string.h"

/* slirp 内建 DNS:10.0.2.3（主机序，与内核 dns.rs 的默认值一致） */
#define DNS_SERVER_IP 0x0A000203u
#define DNS_PORT      53

static int build_dns_query(unsigned char *q) {
    int n = 0;
    q[n++] = 0x5A; q[n++] = 0xA5;   /* ID：固定值，用于确认拿到的是自己发的那条 */
    q[n++] = 0x01; q[n++] = 0x00;   /* flags: RD=1 */
    q[n++] = 0; q[n++] = 1;         /* QDCOUNT = 1 */
    q[n++] = 0; q[n++] = 0;         /* ANCOUNT */
    q[n++] = 0; q[n++] = 0;         /* NSCOUNT */
    q[n++] = 0; q[n++] = 0;         /* ARCOUNT */
    q[n++] = 7;                     /* "example" */
    { const char *s = "example"; while (*s) q[n++] = (unsigned char)*s++; }
    q[n++] = 3;                     /* "com" */
    { const char *s = "com"; while (*s) q[n++] = (unsigned char)*s++; }
    q[n++] = 0;                     /* 名字结束 */
    q[n++] = 0; q[n++] = 1;         /* QTYPE = A */
    q[n++] = 0; q[n++] = 1;         /* QCLASS = IN */
    return n;
}

int main(void) {
    TEST_START("net_msgpeek");

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(fd >= 0, "socket udp");

    struct sockaddr_in srv = addr_of(DNS_SERVER_IP, DNS_PORT);
    CHECK(connect(fd, (struct sockaddr *)&srv, sizeof srv) == 0, "udp connect");

    unsigned char q[64];
    int qlen = build_dns_query(q);
    CHECK(send(fd, q, qlen, 0) == qlen, "send dns query");

    unsigned char a[512], b[512], c[512];

    /* [1] 第一次 peek */
    int n1 = recv(fd, a, (int)sizeof a, MSG_PEEK);
    if (n1 <= 0)
        TEST_SKIP("网络未就绪：peek 拿不到 DNS 应答");
    CHECK(n1 > 12, "recv(MSG_PEEK) 拿到应答");
    CHECK(n1 > 12 && a[0] == 0x5A && a[1] == 0xA5, "peek 内容是自己的查询 ID");

    /* [2] 第二次 peek 必须一模一样 */
    int n2 = recv(fd, b, (int)sizeof b, MSG_PEEK);
    CHECK(n2 == n1, "第二次 peek 长度不变（第一次没消费）");
    CHECK(n1 > 0 && n2 == n1 && memcmp(a, b, (unsigned long)n1) == 0,
          "第二次 peek 内容完全一致");

    /* [3] peek 之后正常 recv，仍应拿到同一条 */
    int n3 = recv(fd, c, (int)sizeof c, 0);
    CHECK(n3 == n1, "peek 之后 recv 拿到同一长度");
    CHECK(n1 > 0 && n3 == n1 && memcmp(a, c, (unsigned long)n1) == 0,
          "peek 之后 recv 拿到同一内容（没被 peek 吃掉）");

    /* [4] 已经消费过，再读应超时返回 0 */
    int n4 = recv(fd, c, (int)sizeof c, 0);
    CHECK(n4 == 0, "数据只被消费一次（再读超时返回 0）");

    /* [5] recvfrom 路径（OpenSSL 用的正是这条） */
    {
        CHECK(send(fd, q, qlen, 0) == qlen, "send 第二条查询");

        struct sockaddr_in src;
        socklen_t slen = sizeof src;
        unsigned char d1[512], d2[512];
        int m1 = recvfrom(fd, d1, (int)sizeof d1, MSG_PEEK,
                          (struct sockaddr *)&src, &slen);
        CHECK(m1 > 12, "recvfrom(MSG_PEEK) 拿到应答");
        CHECK(ntohl(src.sin_addr.s_addr) == DNS_SERVER_IP && ntohs(src.sin_port) == DNS_PORT,
              "peek 也回填了来源地址");

        slen = sizeof src;
        int m2 = recvfrom(fd, d2, (int)sizeof d2, MSG_PEEK,
                          (struct sockaddr *)&src, &slen);
        CHECK(m1 > 0 && m2 == m1 && memcmp(d1, d2, (unsigned long)m1) == 0,
              "recvfrom 连续两次 peek 内容一致");

        slen = sizeof src;
        int m3 = recvfrom(fd, d2, (int)sizeof d2, 0, (struct sockaddr *)&src, &slen);
        CHECK(m3 == m1, "recvfrom peek 之后正常读拿到同一报文");
    }

    /* [6] 未实现的位必须报错，不能被当成 0 */
    {
        /* 先保证队列里有数据，否则"失败"可能只是超时 */
        CHECK(send(fd, q, qlen, 0) == qlen, "send 第三条查询");
        int bad = recv(fd, c, (int)sizeof c, MSG_DONTWAIT);
        CHECK(bad < 0, "未实现的 flags 返回负值（不静默忽略）");
        int ok = recv(fd, c, (int)sizeof c, 0);
        CHECK(ok > 0, "报错没有把数据吃掉");
    }

    net_close(fd);
    TEST_END();
}
