/*
 * netinet/in.h —— IPv4 协议常量与地址结构
 *
 * struct in_addr 是**布局兼容**的补充，不替换 net.h 里 sockaddr_in 的
 * `unsigned int sin_addr`：
 *   - net.h 的现有用户代码（8 处）直接对 sin_addr 做算术语义运算
 *     （ntohl(a.sin_addr)、&sin_addr 取字节），改成结构体会连带改一圈；
 *   - 而 struct in_addr 只有 4 字节、对齐 4，与 unsigned int 完全同布局，
 *     所以 `*(struct in_addr *)&sa.sin_addr` 这类写法（git 的 connect.c 就这么用）
 *     是安全的。
 *
 * 代价：想写 `sin.sin_addr.s_addr = ...` 的代码不能直接编过（git 里只有
 * daemon.c / imap-send.c 这样写，而这两个本来就不进本地 git 的构建）。
 */
#ifndef RK_NETINET_IN_H
#define RK_NETINET_IN_H

#include "../sys/socket.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int in_addr_t;
typedef unsigned short in_port_t;

struct in_addr {
    in_addr_t s_addr;      /* 网络字节序 */
};

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

#ifdef __cplusplus
}
#endif

#endif /* RK_NETINET_IN_H */
