/*
 * netinet/in.h —— IPv4 协议常量与地址结构
 *
 * `in_addr_t` / `in_port_t` / `struct in_addr` 的**定义在 net.h**，这里只做
 * 包含与说明，不重复定义：sockaddr_in 需要 struct in_addr，而包含方向是单向的
 *     netinet/in.h -> sys/socket.h -> net.h
 * 所以定义只能在下游的 net.h 里（详见那边的注释）。
 *
 * 历史教训：sockaddr_in.sin_addr 曾经被声明成裸 `unsigned int`（理由是"现有代码
 * 直接做算术运算，改成结构体要连带改一圈"）。这省了 8 处改动，代价是任何按
 * POSIX 写 `sa.sin_addr.s_addr` 的代码都编不过 —— 包括 git 的 daemon.c、
 * OpenSSL 的 bio_addr.c。真正的标准写法反而是**少数**要迁就的，所以已改正。
 * 布局上 struct in_addr 只含一个 u32，ABI 与原来的 unsigned int 完全一致。
 */
#ifndef RK_NETINET_IN_H
#define RK_NETINET_IN_H

#include "../sys/socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- IPv6 地址：只有**类型**，没有功能 ----
 */
struct in6_addr {
    union {
        unsigned char  __u6_addr8[16];
        unsigned short __u6_addr16[8];
        unsigned int   __u6_addr32[4];
    } __u6_addr;
};
#define s6_addr   __u6_addr.__u6_addr8
#define s6_addr16 __u6_addr.__u6_addr16
#define s6_addr32 __u6_addr.__u6_addr32

struct sockaddr_in6 {
    sa_family_t     sin6_family;    /* AF_INET6 */
    in_port_t       sin6_port;      /* 网络字节序 */
    unsigned int    sin6_flowinfo;
    struct in6_addr sin6_addr;
    unsigned int    sin6_scope_id;
};

/* 常量形式的全零/回环地址（glibc 的 IN6ADDR_*_INIT 是三层花括号，对应
 * struct -> union -> 数组）。指针形式的 in6addr_any 在这里用不到就不引入，
 * 免得变成"声明了但没有定义"的链接期意外。 */
#define IN6ADDR_ANY_INIT      { { { 0 } } }
#define IN6ADDR_LOOPBACK_INIT { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }

/* inet_ntop / inet_pton 的缓冲区上限（POSIX 在 <netinet/in.h> 里给） */
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN  16    /* "255.255.255.255" + NUL */
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46    /* 最长 "xxxx:...:255.255.255.255" + NUL */
#endif

/* INADDR_* 都是**主机序**，用的时候要 htonl()（POSIX 语义） */
#define INADDR_ANY       0x00000000u
#define INADDR_BROADCAST 0xffffffffu
#define INADDR_NONE      0xffffffffu
#define INADDR_LOOPBACK  0x7f000001u

#ifndef IPPROTO_IP
#define IPPROTO_IP 0
#endif
#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP 1
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif
#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif

#ifdef __cplusplus
}
#endif

#endif /* RK_NETINET_IN_H */
