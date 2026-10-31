/*
 * net/if.h —— 网络接口名与 ioctl 请求（只有结构体与常量）
 *
 * 现状：没有接口枚举能力。内核网卡是单块 virtio-net，没有"多接口"这个概念，
 * 也没有 SIOCGIFCONF / getifaddrs 的支持。
 *
 * 布局对齐 Linux：ifr_name 16 字节 + union（最大成员是 char* / int，8 字节），
 * 总量 24 字节。真去 ioctl 会拿到 ENOTTY，不会踩内存。
 */
#ifndef RK_NET_IF_H
#define RK_NET_IF_H

#include "../net.h"     /* struct sockaddr */

#ifdef __cplusplus
extern "C" {
#endif

#define IFNAMSIZ   16
#define IF_NAMESIZE IFNAMSIZ

struct ifreq {
    char ifr_name[IFNAMSIZ];
    union {
        struct sockaddr ifru_addr;
        struct sockaddr ifru_dstaddr;
        struct sockaddr ifru_broadaddr;
        struct sockaddr ifru_netmask;
        struct sockaddr ifru_hwaddr;
        short           ifru_flags;
        int             ifru_ivalue;
        int             ifru_mtu;
        char           *ifru_data;
    } ifr_ifru;
};

#define ifr_addr      ifr_ifru.ifru_addr
#define ifr_dstaddr   ifr_ifru.ifru_dstaddr
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_netmask   ifr_ifru.ifru_netmask
#define ifr_hwaddr    ifr_ifru.ifru_hwaddr
#define ifr_flags     ifr_ifru.ifru_flags
#define ifr_mtu       ifr_ifru.ifru_mtu
#define ifr_data      ifr_ifru.ifru_data

/* 常用的接口标志位。值照 asm-generic/if.h，纯编译期常量，不涉及实现。 */
#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_DEBUG       0x4
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_NOTRAILERS  0x20
#define IFF_RUNNING     0x40
#define IFF_NOARP       0x80
#define IFF_PROMISC     0x100
#define IFF_ALLMULTI    0x200
#define IFF_MULTICAST   0x1000

/* ioctl 请求号（Linux 数值）。内核没有 netdev 层，这些请求会返回 ENOTTY；
 * 列出来是为了让 `#ifdef SIOCGIFADDR` 之类的判断成立后能编过。 */
#define SIOCGIFCONF    0x8912
#define SIOCGIFFLAGS   0x8913
#define SIOCSIFFLAGS   0x8914
#define SIOCGIFADDR    0x8915
#define SIOCSIFADDR    0x8916
#define SIOCGIFDSTADDR 0x8917
#define SIOCGIFBRDADDR 0x8919
#define SIOCGIFNETMASK 0x891b
#define SIOCGIFMETRIC  0x891d
#define SIOCGIFMTU     0x8921
#define SIOCGIFHWADDR  0x8927
#define SIOCGIFINDEX   0x8933
#define SIOCGIFNAME    0x8910

static inline unsigned int if_nametoindex(const char *name) {
    (void)name;
    return 0;                  /* 没有多接口，任何名字都对应不上 */
}

static inline char *if_indextoname(unsigned int index, char *buf) {
    (void)index; (void)buf;
    return (char *)0;
}

#ifdef __cplusplus
}
#endif

#endif /* RK_NET_IF_H */
