/*
 * netinet/tcp.h —— TCP 层选项
 *
 * 只为编译兼容：内核的 socket 实现不接受 setsockopt，
 * 所以这些宏目前没有实际作用（set/netsockopt 不存在）。
 */
#ifndef RK_NETINET_TCP_H
#define RK_NETINET_TCP_H

#define TCP_NODELAY   1
#define TCP_MAXSEG    2
#define TCP_KEEPIDLE  4
#define TCP_KEEPINTVL 5
#define TCP_KEEPCNT   6

#endif /* RK_NETINET_TCP_H */
