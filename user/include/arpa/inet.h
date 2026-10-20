/*
 * arpa/inet.h —— 地址表示与解析（IPv4）
 *
 * inet_pton / inet_ntop / inet_addr / inet_ntoa 全部 static inline，
 * 不引入任何链接期符号 —— 这样即使不编 git 的 compat/inet_*.c 也能用。
 *
 * 只支持 AF_INET：内核 socket 也只支持 IPv4，返回 EAFNOSUPPORT 比
 * "假装成功" 诚实。
 */
#ifndef RK_ARPA_INET_H
#define RK_ARPA_INET_H

#include "../sys/socket.h"
#include "../netinet/in.h"
#include "../errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "192.168.1.1" -> 4 字节网络序。返回 1 成功 / 0 格式非法 / -1 不支持该家族 */
static inline int inet_pton(int af, const char *src, void *dst) {
    if (af != AF_INET) { errno = EAFNOSUPPORT; return -1; }
    if (!src || !dst)  { errno = EINVAL;        return -1; }

    const char *p = src;
    unsigned char out[4];

    for (int i = 0; i < 4; i++) {
        if (*p < '0' || *p > '9') return 0;      /* 每段必须以数字开头 */
        int v = 0, digits = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            if (++digits > 3 || v > 255) return 0;
            p++;
        }
        out[i] = (unsigned char)v;
        if (i < 3) {
            if (*p != '.') return 0;
            p++;
        }
    }
    if (*p != '\0') return 0;                    /* 末尾不能有多余字符 */

    for (int i = 0; i < 4; i++) ((unsigned char *)dst)[i] = out[i];
    return 1;
}

/* 4 字节网络序 -> "a.b.c.d"。size 不够时返回 NULL 并置 ENOSPC */
static inline const char *inet_ntop(int af, const void *src, char *dst, unsigned int size) {
    if (af != AF_INET)  { errno = EAFNOSUPPORT; return 0; }
    if (!src || !dst)   { errno = EINVAL;       return 0; }

    const unsigned char *b = (const unsigned char *)src;
    char tmp[16];
    unsigned int n = 0;

    for (int i = 0; i < 4; i++) {
        unsigned int v = b[i];
        char d[3];
        int k = 0;
        if (v == 0) {
            d[k++] = '0';
        } else {
            while (v) { d[k++] = (char)('0' + (v % 10)); v /= 10; }
        }
        while (k > 0) tmp[n++] = d[--k];
        if (i < 3) tmp[n++] = '.';
    }

    if (n + 1 > size) { errno = ENOSPC; return 0; }
    for (unsigned int i = 0; i < n; i++) dst[i] = tmp[i];
    dst[n] = '\0';
    return dst;
}

/* 返回**网络序**整数；非法输入返回 INADDR_NONE（0xffffffff） */
static inline in_addr_t inet_addr(const char *cp) {
    unsigned char b[4];
    if (inet_pton(AF_INET, cp, b) != 1) return INADDR_NONE;
    return  (in_addr_t)b[0]
         | ((in_addr_t)b[1] << 8)
         | ((in_addr_t)b[2] << 16)
         | ((in_addr_t)b[3] << 24);
}

/* 非线程安全（POSIX 允许）：返回静态缓冲区 */
static inline char *inet_ntoa(struct in_addr in) {
    static char buf[16];
    if (!inet_ntop(AF_INET, &in, buf, sizeof buf)) return (char *)0;
    return buf;
}

#ifdef __cplusplus
}
#endif

#endif /* RK_ARPA_INET_H */
