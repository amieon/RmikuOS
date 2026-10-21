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
#include "errno.h"    /* bind/connect 要把"非 AF_INET"报成 EAFNOSUPPORT */

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
typedef unsigned int   socklen_t;
typedef unsigned short sa_family_t;

/* 通用地址头：POSIX 规定所有具体地址结构都以它为前缀。
 * 定义放在 net.h（而不是 sys/socket.h）是因为后面的 bind/connect 要按
 * sa_family 分派 —— 写成 sys/socket.h 会变成"socket.h 包含 net.h，
 * net.h 又需要 socket.h"，循环包含。sys/socket.h 只保留地址族宏与 shutdown。 */
struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct sockaddr_in {
    sa_family_t    sin_family;   /* AF_INET */
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

/* 把通用 sockaddr 收窄成 sockaddr_in（公共入口统一走这里）。
 *
 * 内核只有 IPv4 的 (ip, port) 通道，所以非 AF_INET 一律诚实失败 EAFNOSUPPORT，
 * 而不是拿 sockaddr_in 的布局去读 AF_UNIX 的路径。 */
static inline const struct sockaddr_in *rk_sockaddr_in(const struct sockaddr *addr) {
    if (!addr)                        { errno = EINVAL;         return (const struct sockaddr_in *)0; }
    if (addr->sa_family != AF_INET)   { errno = EAFNOSUPPORT;   return (const struct sockaddr_in *)0; }
    return (const struct sockaddr_in *)addr;
}

/* ---- 与内核 info 缓冲一致的临时结构(accept/recvfrom 回传用) ----
 * 注意:内核侧固定写入 8 字节(ip 4B + port 2B + 预留 2B),
 * 这里必须也是 8 字节,否则越界踩栈。 */
struct net_peer { unsigned char raw[8]; };

/* ---- socket ---- */
static inline int socket(int domain, int type, int protocol) {
    (void)domain;   /* 只支持 AF_INET,内核暂不区分 */
    return syscall3(SYS_NET_SOCKET0, type, protocol, 0);
}
static inline int socket_tcp(void)         { return socket(AF_INET, SOCK_STREAM, 0); }
static inline int socket_udp(void)         { return socket(AF_INET, SOCK_DGRAM, 0); }
static inline int socket_raw(int protocol) { return socket(AF_INET, SOCK_RAW, protocol); }

/* ---- bind / listen / accept / connect ----
 * 参数一律是 POSIX 的 `const struct sockaddr *`，内部收窄成 sockaddr_in。 */
static inline int bind(int fd, const struct sockaddr *addr, socklen_t len) {
    (void)len;
    const struct sockaddr_in *in = rk_sockaddr_in(addr);
    if (!in) return -1;
    return syscall3(SYS_NET_BIND, fd, ntohs(in->sin_port), 0);
}
static inline int listen(int fd, int backlog) {
    return syscall3(SYS_NET_LISTEN, fd, backlog, 0);
}
static inline int connect(int fd, const struct sockaddr *addr, socklen_t len) {
    (void)len;
    const struct sockaddr_in *in = rk_sockaddr_in(addr);
    if (!in) return -1;
    return syscall3(SYS_NET_CONNECT, fd, ntohl(in->sin_addr), ntohs(in->sin_port));
}
static inline int accept(int fd, struct sockaddr *addr, socklen_t *len) {
    struct net_peer p;
    (void)len;
    int ret = syscall3(SYS_NET_ACCEPT, fd, (long)&p, 0);
    if (ret >= 0 && addr) {
        /* 内核写回主机序(to_ne_bytes,小端:低字节在前),按小端拼回再转网络序 */
        unsigned int ip = (unsigned)p.raw[0]
                        | ((unsigned)p.raw[1] << 8)
                        | ((unsigned)p.raw[2] << 16)
                        | ((unsigned)p.raw[3] << 24);
        unsigned short port = (unsigned short)((unsigned)p.raw[4]
                        | ((unsigned)p.raw[5] << 8));
        struct sockaddr_in *in = (struct sockaddr_in *)addr;
        in->sin_family = AF_INET;
        in->sin_addr   = htonl(ip);
        in->sin_port   = htons(port);
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
                         const struct sockaddr *dst, socklen_t dlen) {
    (void)flags; (void)dlen;
    const struct sockaddr_in *in = rk_sockaddr_in(dst);
    if (!in) return -1;
    return syscall6(SYS_NET_SENDTO0, fd, (long)buf, len,
                    ntohl(in->sin_addr), ntohs(in->sin_port), 0);
}
static inline int recvfrom(int fd, void *buf, int len, int flags,
                           struct sockaddr *src, socklen_t *slen) {
    struct net_peer p;
    (void)flags; (void)slen;
    int ret = syscall6(SYS_NET_RECVFROM, fd, (long)buf, len, (long)&p, 0, 0);
    if (ret >= 0 && src) {
        /* 与 accept 相同:小端拼回主机序,再转网络序填 sockaddr_in */
        unsigned int ip = (unsigned)p.raw[0]
                        | ((unsigned)p.raw[1] << 8)
                        | ((unsigned)p.raw[2] << 16)
                        | ((unsigned)p.raw[3] << 24);
        unsigned short port = (unsigned short)((unsigned)p.raw[4]
                        | ((unsigned)p.raw[5] << 8));
        struct sockaddr_in *in = (struct sockaddr_in *)src;
        in->sin_family = AF_INET;
        in->sin_addr   = htonl(ip);
        in->sin_port   = htons(port);
    }
    return ret;
}

/* 已经拿下 socket fd 与文件 fd 的统一 fd 表 */
static inline int net_close(int fd) {
    return syscall3(SYS_NET_CLOSE, fd, 0, 0);
}

/* SYS_NET_SET_IP 的编号与内核一致 */
static inline int net_set_ip(unsigned int ip) {   /* 主机序 u32 */
    return syscall3(SYS_NET_SET_IP, ip, 0, 0);
}
static inline unsigned int net_get_ip() {
    return (unsigned int)syscall3(SYS_NET_GET_IP, 0, 0, 0);
}

/* DNS 解析:"www.example.com" -> 主机序 IPv4;失败返回 0(0.0.0.0 视为失败) */
static inline unsigned int net_resolve(const char *name) {
    unsigned int n = 0;
    while (name[n]) n++;
    long r = syscall3(SYS_NET_RESOLVE, (long)name, n, 0);
    return r < 0 ? 0u : (unsigned int)r;   /* 内核失败返回 -1,别让它强转成 0xFFFFFFFF */
}

static inline int net_resolve_many(const char *names[], unsigned long lens[],
                                   unsigned int out[], int count) {
    return (int)syscall6(SYS_NET_RESOLVE_MANY, (long)names, (long)lens,
                         (long)out, (long)count, 0, 0);
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

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

static inline int net_shutdown(int fd, int how) {
    return syscall3(SYS_NET_SHUTDOWN, fd, how,0);   // 按你的 syscall 封装宏调整
}

/* ---- socket 层选项 ----
 * SOL_SOCKET 表示"这个选项属于 socket 层"（与 Linux 一致，都是 1）。
 * SO_* 的数值和内核 drivers/net/socket.rs 用的是同一套：
 *   注意 SO_REUSEADDR 这里是 1，而 Linux 是 2 —— 想对齐 Linux 必须**同时**
 *   改内核那个常量，只改一边会让 net_setsockopt 静默失败。 */
#define SOL_SOCKET    1
#define SO_REUSEADDR  1
#define SO_KEEPALIVE  9    /* 只为让可移植代码编过；内核没有 TCP keepalive */

static inline int net_getsockname(int fd, struct sockaddr_in *addr) {
    struct net_peer p;
    if (syscall3(SYS_NET_GETSOCKNAME, fd, (long)&p, 0) < 0) return -1;
    unsigned int ip = (unsigned)p.raw[0] | ((unsigned)p.raw[1] << 8)
                    | ((unsigned)p.raw[2] << 16) | ((unsigned)p.raw[3] << 24);
    unsigned short port = (unsigned short)((unsigned)p.raw[4] | ((unsigned)p.raw[5] << 8));
    addr->sin_family = AF_INET;
    addr->sin_addr   = htonl(ip);
    addr->sin_port   = htons(port);
    return 0;
}

static inline int net_getpeername(int fd, struct sockaddr_in *addr) {
    struct net_peer p;
    if (syscall3(SYS_NET_GETPEERNAME, fd, (long)&p, 0) < 0) return -1;
    unsigned int ip = (unsigned)p.raw[0] | ((unsigned)p.raw[1] << 8)
                | ((unsigned)p.raw[2] << 16) | ((unsigned)p.raw[3] << 24);
    unsigned short port = (unsigned short)((unsigned)p.raw[4] | ((unsigned)p.raw[5] << 8));
    addr->sin_family = AF_INET;          /* 原来漏了这行，读 family 的代码会拿到垃圾 */
    addr->sin_addr   = htonl(ip);
    addr->sin_port   = htons(port);
    return 0;
}

/* ---- POSIX 名字的 getsockname / getpeername ----
 * 传的是通用 sockaddr，回填时按 sockaddr_in 布局写（内核只有 IPv4）。 */
static inline int getsockname(int fd, struct sockaddr *addr, socklen_t *len) {
    struct sockaddr_in in;
    if (!addr) { errno = EINVAL; return -1; }
    if (net_getsockname(fd, &in) < 0) return -1;
    *(struct sockaddr_in *)addr = in;
    if (len) *len = sizeof(in);
    return 0;
}

static inline int getpeername(int fd, struct sockaddr *addr, socklen_t *len) {
    struct sockaddr_in in;
    if (!addr) { errno = EINVAL; return -1; }
    if (net_getpeername(fd, &in) < 0) return -1;
    *(struct sockaddr_in *)addr = in;
    if (len) *len = sizeof(in);
    return 0;
}

static inline int net_setsockopt(int fd, int optname, int optval) {
    return syscall3(SYS_NET_SETSOCKOPT, fd, optname, optval);
}

/* POSIX 名字的 setsockopt：多一个 level 和一个 (optval, optlen)。
 *   - level 只认 SOL_SOCKET（内核没有 IPPROTO_IP 那一层选项）
 *   - 只实现 SO_REUSEADDR；其余（比如 SO_KEEPALIVE）**明确返回 ENOPROTOOPT**，
 *     不假装成功 —— git 的 enable_keepalive 只在失败时打一条 error 日志，
 *     不会因此中断连接，所以诚实拒绝是安全的。
 * optlen 小于 sizeof(int) 时按 0 处理（调用方没给值就别猜）。 */
static inline int setsockopt(int fd, int level, int optname,
                            const void *optval, socklen_t optlen) {
    if (level != SOL_SOCKET) {
        errno = ENOPROTOOPT;
        return -1;
    }
    int on = (optval && optlen >= (socklen_t)sizeof(int)) ? *(const int *)optval : 0;
    if (optname == SO_REUSEADDR) {
        return net_setsockopt(fd, SO_REUSEADDR, on);
    }
    errno = ENOPROTOOPT;
    return -1;
}


#ifdef __cplusplus
}
#endif