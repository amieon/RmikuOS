/*
 * ifaddrs.h —— 接口地址枚举
 *
 * 现状：结构体齐全，函数明确失败（errno = ENOSYS）。
 *
 * configure 的 AC_CHECK_FUNCS(getifaddrs) 是链接期检测，我们这没有
 * getifaddrs 的实体符号（static inline 不算），所以 HAVE_GETIFADDRS 不会
 * 定义，libcurl 里那段代码根本不参与编译。这个头只是让
 * `#ifdef HAVE_IFADDRS_H` 成立时有东西可 include。
 */
#ifndef RK_IFADDRS_H
#define RK_IFADDRS_H

#include "net.h"        /* struct sockaddr */
#include "errno.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ifaddrs {
    struct ifaddrs  *ifa_next;
    char            *ifa_name;
    unsigned int     ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    union {
        struct sockaddr *ifu_broadaddr;
        struct sockaddr *ifu_dstaddr;
    } ifa_ifu;
    void            *ifa_data;
};

#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr   ifa_ifu.ifu_dstaddr

static inline int getifaddrs(struct ifaddrs **ifap) {
    if (ifap) *ifap = (struct ifaddrs *)0;
    errno = ENOSYS;
    return -1;
}

static inline void freeifaddrs(struct ifaddrs *ifa) { (void)ifa; }

#ifdef __cplusplus
}
#endif

#endif /* RK_IFADDRS_H */
