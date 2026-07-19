/*
 * net.h —— POSIX 风格 socket 接口(用户态)
 *
 * 设计约定:
 * - 地址一律用 struct sockaddr_in,字段为【网络字节序】(POSIX 语义);
 * - 内核 syscall 参数仍为【主机序】 u32 ip / u16 port,转换在本头文件内完成;
 * - stype: 1=STREAM(TCP) 2=DGRAM(UDP) 3=RAW;protocol=0 表默认;
 * - close 暂不纳入(等 fd 表统一后用文件系统的 close,删掉 net_close)。
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"

/* ---- 常量(POSIX 对齐) ---- */
#define AF_INET      2
#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define SOCK_RAW     3
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

/* ---- 字节序(主机为小端) ---- */
static inline unsigned short htons(unsigned short x) { return (x >> 8) | (x << 8); }
static inline unsigned short ntohs(unsigned short x) { return htons(x); }
static inline unsigned int  htonl(unsigned int x) {
    return ((x & 0xff) << 24) | ((x & 0xff00) << 8)
         | ((x >> 8) & 0xff00) | ((x >> 24) & 0xff);
}
static inline unsigned int  ntohl(unsigned int x) { return htonl(x); }

/* ---- 地址结构 ---- */
typedef unsigned int socklen_t;

struct sockaddr_in {
    unsigned short sin_family;   /* AF_INET */
    unsigned short sin_port;     /* 网络字节序 */
    unsigned int   sin_addr;     /* 网络字节序 */
    char           sin_zero[8];
};

/* 便捷构造:传【主机序】 ip/port,返回填好的 sockaddr_in */
static inline struct sockaddr_in addr_of(unsigned int ip, unsigned short port) {
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_port   = htons(port);
    a.sin_addr   = htonl(ip);
    return a;
}

/* ---- 与内核 SocketAddr 布局一致的临时结构(accept/recvfrom 回传用) ---- */
struct net_peer { unsigned char raw[6]; };

/* ---- socket ---- */
static inline int socket(int domain, int type, int protocol) {
    (void)domain;   /* 只支持 AF_INET,内核暂不区分 */
    return syscall3(SYS_NET_SOCKET0, type, protocol, 0);
}
static inline int socket_tcp(void)         { return socket(AF_INET, SOCK_STREAM, 0); }
static inline int socket_udp(void)         { return socket(AF_INET, SOCK_DGRAM, 0); }
static inline int socket_raw(int protocol) { return socket(AF_INET, SOCK_RAW, protocol); }

/* ---- bind / listen / accept / connect ---- */
static inline int bind(int fd, const struct sockaddr_in *addr, socklen_t len) {
    (void)len;
    return syscall3(SYS_NET_BIND, fd, ntohs(addr->sin_port), 0);
}
static inline int listen(int fd, int backlog) {
    return syscall3(SYS_NET_LISTEN, fd, backlog, 0);
}
static inline int connect(int fd, const struct sockaddr_in *addr, socklen_t len) {
    (void)len;
    return syscall3(SYS_NET_CONNECT, fd, ntohl(addr->sin_addr), ntohs(addr->sin_port));
}
static inline int accept(int fd, struct sockaddr_in *addr, socklen_t *len) {
    struct net_peer p;
    (void)len;
    int ret = syscall3(SYS_NET_ACCEPT, fd, (long)&p, 0);
    if (ret >= 0 && addr) {
        addr->sin_family = AF_INET;
        addr->sin_addr = htonl((unsigned)p.raw[0] << 24 | p.raw[1] << 16 | p.raw[2] << 8 | p.raw[3]);
        addr->sin_port = htons((p.raw[4] << 8) | p.raw[5]);
    }
    return ret;
}

/* ---- 收发 ---- */
static inline int send(int fd, const void *buf, int len, int flags) {
    (void)flags;
    return syscall3(SYS_NET_SEND, fd, (long)buf, len);
}
static inline int recv(int fd, void *buf, int len, int flags) {
    (void)flags;
    return syscall3(SYS_NET_RECV, fd, (long)buf, len);
}
static inline int sendto(int fd, const void *buf, int len, int flags,
                         const struct sockaddr_in *dst, socklen_t dlen) {
    (void)flags; (void)dlen;
    return syscall6(SYS_NET_SENDTO0, fd, (long)buf, len,
                    ntohl(dst->sin_addr), ntohs(dst->sin_port), 0);
}
static inline int recvfrom(int fd, void *buf, int len, int flags,
                           struct sockaddr_in *src, socklen_t *slen) {
    struct net_peer p;
    (void)flags; (void)slen;
    int ret = syscall6(SYS_NET_RECVFROM, fd, (long)buf, len, (long)&p, 0, 0);
    if (ret >= 0 && src) {
        src->sin_family = AF_INET;
        src->sin_addr = htonl((unsigned)p.raw[0] << 24 | p.raw[1] << 16 | p.raw[2] << 8 | p.raw[3]);
        src->sin_port = htons((p.raw[4] << 8) | p.raw[5]);
    }
    return ret;
}

/* close: 暂保留 net_close,待 fd 表统一后并入文件系统 close */
static inline int net_close(int fd) {
    return syscall3(SYS_NET_CLOSE, fd, 0, 0);
}

/* SYS_NET_SET_IP 的编号与内核一致 */
static inline int net_set_ip(unsigned int ip) {   /* 主机序 u32 */
    return syscall3(SYS_NET_SET_IP, ip, 0, 0);
}

/* "192.168.100.1" -> 主机序 u32;非法返回 0(0.0.0.0 也视为非法,反正没用) */
static inline unsigned int parse_ip(const char *s) {
    unsigned int v = 0;
    for (int part = 0; part < 4; part++) {
        unsigned int n = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (*s - '0');
            if (n > 255) return 0;
            s++; digits++;
        }
        if (!digits) return 0;
        v = (v << 8) | n;
        if (part < 3) {
            if (*s != '.') return 0;
            s++;
        } else if (*s) return 0;   /* 尾部垃圾 */
    }
    return v;
}

#ifdef __cplusplus
}
#endif