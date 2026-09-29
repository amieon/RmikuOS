/*
 * net_udp_connect_test.c —— UDP connect() / send() / recv() 与临时端口分配器测试
 *
 * 覆盖:
 *   [1] 未 connect 的 UDP socket 直接 send 必须失败(无默认对端,类 EDESTADDRREQ)
 *   [2] connect(10.0.2.3:53) 成功(slirp 内建 DNS 服务器)
 *   [3] connect 后自动分配 IANA 临时端口段(49152+)的本端端口
 *   [4] getpeername 回读默认对端 == 10.0.2.3:53
 *   [5] send()/recv() 走通一次真实 DNS 查询(验证 connect 后收发路径 + 来源过滤放行)
 *   [6] connect(0.0.0.0:0) 断连后 send 再次失败
 *
 * 依赖网络就绪(DHCP 已拿到地址);无网环境下 [5] 会因收不到应答而 FAIL。
 */
#include "test.h"
#include "net.h"

/* slirp 内建 DNS:10.0.2.3(主机序,与内核 dns.rs 的默认值一致) */
#define DNS_SERVER_IP 0x0A000203u
#define DNS_PORT      53

/* 手工拼一个 example.com 的 A 记录查询,返回报文长度 */
static int build_dns_query(unsigned char *q) {
    int n = 0;
    q[n++] = 0x12; q[n++] = 0x34;   /* ID */
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
    TEST_START("net_udp_connect");

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(fd >= 0, "socket udp");

    /* [1] 未 connect 直接 send -> -1 */
    char one = 0x42;
    CHECK(send(fd, &one, 1, 0) < 0, "send before connect fails");

    /* [2] connect 到 10.0.2.3:53 */
    struct sockaddr_in srv = addr_of(DNS_SERVER_IP, DNS_PORT);
    CHECK(connect(fd, &srv, sizeof srv) == 0, "udp connect");

    /* [3] getsockname:autobind 的端口落在 49152+ 临时端口段 */
    struct sockaddr_in mine;
    CHECK(net_getsockname(fd, &mine) == 0, "getsockname");
    {
        unsigned int lp = ntohs(mine.sin_port);
        CHECK(lp >= 49152, "autobind port in ephemeral range (49152+)");
    }

    /* [4] getpeername 回读默认对端 */
    struct sockaddr_in peer;
    CHECK(net_getpeername(fd, &peer) == 0, "getpeername");
    CHECK(ntohl(peer.sin_addr) == DNS_SERVER_IP && ntohs(peer.sin_port) == DNS_PORT,
          "peer == 10.0.2.3:53");

    /* [5] send()/recv() 真实 DNS 查询(不经过 sendto/recvfrom) */
    {
        unsigned char q[64];
        int qlen = build_dns_query(q);
        CHECK(send(fd, q, qlen, 0) == qlen, "send dns query via send()");

        unsigned char resp[512];
        int r = recv(fd, resp, (int)sizeof resp, 0);
        CHECK(r > 12, "recv dns reply via recv()");
        if (r > 12) {
            CHECK(resp[0] == 0x12 && resp[1] == 0x34, "reply ID matches query");
            CHECK(((resp[6] << 8) | resp[7]) > 0, "reply ANCOUNT > 0");
        }
    }

    /* [6] connect(0.0.0.0:0) 断连,send 恢复失败 */
    {
        struct sockaddr_in none_ = addr_of(0, 0);
        CHECK(connect(fd, &none_, sizeof none_) == 0, "disconnect via connect(0.0.0.0:0)");
        CHECK(send(fd, &one, 1, 0) < 0, "send after disconnect fails");
    }

    net_close(fd);
    TEST_END();
}
