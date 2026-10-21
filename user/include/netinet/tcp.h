/*
 * netinet/tcp.h —— TCP 层选项
 *
 * 只为编译兼容：内核的 setsockopt 只认 SOL_SOCKET 层的 SO_REUSEADDR
 * （见 net.h），IPPROTO_TCP 这一层的选项一律返回 ENOPROTOOPT。
 * 所以下面这些宏目前只有"能编过"的作用，没有实际行为。
 */
#ifndef RK_NETINET_TCP_H
#define RK_NETINET_TCP_H

#define TCP_NODELAY   1
#define TCP_MAXSEG    2
#define TCP_KEEPIDLE  4
#define TCP_KEEPINTVL 5
#define TCP_KEEPCNT   6

#endif /* RK_NETINET_TCP_H */
