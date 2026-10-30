#pragma once
#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>
#include "mem.h"
#include "io.h"
#include "process.h"
#include "string.h"
#include "stdio.h"
#include "env.h"      /* 真实 getenv/setenv: 基于 SYS_GETENV 系统调用 */
#include "fs.h"       /* mkstemp/mkdtemp：POSIX 归 stdlib，实现在 fs.h */
#include "errno.h"    /* strto* 的 EINVAL/ERANGE（io.h 已间接到，这里写明依赖） */

/* ---- misc (inline) ---- */
static inline void abort(void) {
    printf("abort\n");
    exit(127);
}

/* realpath: POSIX 路径规范化。RmikuOS 无符号链接、路径已规范化;
 * 返回 NULL 让调用方 fallback 原路径(TCC 的 normalized_PATHCMP 等)。 */
static inline char *realpath(const char *path, char *resolved) {
    (void)path; (void)resolved;
    return 0;
}

static inline int abs(int x) {
    return x < 0 ? -x : x;
}

/* 返回类型必须与被除的宽度一致：声明成 int 会在 LP64 下截断 */
static inline long labs(long x) {
    return x < 0 ? -x : x;
}

static inline long long llabs(long long x) {
    return x < 0 ? -x : x;
}

/* ---- strto* 家族 --------------------------------------------------------
 *
 * 只有 64 位：项目仅支持 rv64/la64，long / long long / intmax_t 同宽，
 * 所以下面用 1ULL<<63 这类字面量，不必到处 include <limits.h>。
 *
 * C99 有四条容易被忽略、而 OpenSSL apps/lib/opt.c 全部依赖的规则：
 *   1. 一个数字都没转换到时不消费任何字符，*endp 指回**起点**（不是符号之后）。
 *      opt_intmax 靠 `endp == value` 判非法，否则 "-iter -" 会被当成 0。
 *   2. 溢出必须饱和到 LONG_MAX/LONG_MIN/ULONG_MAX 且 errno=ERANGE，不能静默回绕。
 *      opt_uintmax 靠 `m == UINTMAX_MAX && errno == ERANGE` 拒收；回绕成 0
 *      会被当成合法值（"0x10000000000000000" 曾静默得到 0）。
 *   3. base=0 或 16 时 "0x" 后面没有十六进制数字，前缀不消费，endp 停在 'x'
 *      （'0' 本身仍是合法数字，所以 endp 不是指回起点）。
 *   4. base 不是 0 或 2..36 → errno=EINVAL，endp 指回起点。
 *
 * 旧实现三条全不满足，只是"能解析正常输入"。
 */

static inline int rk_is_xdigit(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* 解析核心：按 base b 累加到无符号 64 位，超过 limit 即饱和。
 * is_signed 只用来定 limit（负数上限取 2^63，刚好容得下 LONG_MIN）。 */
static inline unsigned long long rk_strto_core(const char *s, char **e, int b,
                                               int is_signed,
                                               int *neg_out, int *ovf_out)
{
    const char *start = s;
    int neg = 0, any = 0, ovf = 0, dig;
    unsigned long long acc = 0, limit;

    *neg_out = 0;
    *ovf_out = 0;

    /* 完整六字符空白集：旧实现漏了 \r，CRLF 文本里 "123\r" 的 endp 会停在 \r，
     * 调用方按"整串必须被消费"判断就会误判成非法输入。 */
    while (*s == ' ' || *s == '\t' || *s == '\n' ||
           *s == '\v' || *s == '\r' || *s == '\f')
        s++;
    if (*s == '-')      { neg = 1; s++; }
    else if (*s == '+') { s++; }

    if (b != 0 && (b < 2 || b > 36)) {
        errno = EINVAL;
        if (e) *e = (char *)start;
        return 0;
    }

    if (b == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && rk_is_xdigit(s[2])) {
            b = 16;
            s += 2;              /* 只有真跟着十六进制数字才吃掉 0x */
        } else if (s[0] == '0') {
            b = 8;               /* 不前进：让循环吃掉这个 '0'，纯 "0" 也算转换成功 */
        } else {
            b = 10;
        }
    } else if (b == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && rk_is_xdigit(s[2]))
            s += 2;
    }

    if (!is_signed)  limit = (unsigned long long)-1;      /* ULONG_MAX */
    else if (neg)    limit = 1ULL << 63;                  /* 2^63，容得下 LONG_MIN */
    else             limit = (1ULL << 63) - 1ULL;         /* LONG_MAX */

    for (;;) {
        if      (*s >= '0' && *s <= '9') dig = *s - '0';
        else if (*s >= 'a' && *s <= 'z') dig = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') dig = *s - 'A' + 10;
        else break;
        if (dig >= b) break;
        s++;
        any = 1;
        /* 先比后乘，别让 acc*b 自己溢出；溢出后继续吞数字，
         * 这样 *endp 仍停在整串数字末尾（标准要求）。 */
        if (acc > (limit - (unsigned long long)dig) / (unsigned long long)b) {
            ovf = 1;
            continue;
        }
        acc = acc * (unsigned long long)b + (unsigned long long)dig;
    }

    if (e) *e = (char *)(any ? s : start);
    *neg_out = neg;
    *ovf_out = ovf;
    return acc;
}

static inline long long strtoll(const char *s, char **e, int b) {
    int neg, ovf;
    unsigned long long v = rk_strto_core(s, e, b, 1, &neg, &ovf);

    if (ovf) {
        errno = ERANGE;
        return neg ? (-(long long)((1ULL << 63) - 1ULL) - 1)   /* LONG_MIN */
                   : (long long)((1ULL << 63) - 1ULL);        /* LONG_MAX */
    }
    /* 用无符号取负再转换，避免 -LONG_MIN 这种 UB */
    return neg ? (long long)(0ULL - v) : (long long)v;
}

static inline unsigned long long strtoull(const char *s, char **e, int b) {
    int neg, ovf;
    unsigned long long v = rk_strto_core(s, e, b, 0, &neg, &ovf);

    if (ovf) {
        errno = ERANGE;
        return (unsigned long long)-1;
    }
    return neg ? (0ULL - v) : v;      /* "-1" → 全 1，C99 明文要求 */
}

/* LP64 下 long 与 long long 同宽，直接转 */
static inline long strtol(const char *s, char **e, int b) {
    return (long)strtoll(s, e, b);
}

static inline unsigned long strtoul(const char *s, char **e, int b) {
    return (unsigned long)strtoull(s, e, b);
}

static inline long atol(const char *s) {
    return strtol(s, (char **)0, 10);
}

static inline long long atoll(const char *s) {
    return strtoll(s, (char **)0, 10);
}

/* div/ldiv/lldiv：一次除法同时取商余，调用方只需一次硬件除法 */
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;

static inline div_t div(int num, int den) {
    div_t r; r.quot = num / den; r.rem = num % den; return r;
}

static inline ldiv_t ldiv(long num, long den) {
    ldiv_t r; r.quot = num / den; r.rem = num % den; return r;
}

static inline lldiv_t lldiv(long long num, long long den) {
    lldiv_t r; r.quot = num / den; r.rem = num % den; return r;
}



static inline double strtod(const char *s, char **e) {
    double n = 0.0, sign = 1.0, frac = 0.1;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-')      { sign = -1.0; s++; }
    else if (*s == '+') { s++; }

    while (*s >= '0' && *s <= '9')
        n = n * 10.0 + (*s++ - '0');

    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            n += (*s++ - '0') * frac;
            frac *= 0.1;
        }
    }

    /* 简单指数 e/E */
    if (*s == 'e' || *s == 'E') {
        s++;
        int esign = 1, exp = 0;
        if (*s == '-')      { esign = -1; s++; }
        else if (*s == '+') { s++; }
        while (*s >= '0' && *s <= '9')
            exp = exp * 10 + (*s++ - '0');
        double p = 1.0;
        for (int i = 0; i < exp; i++) p *= 10.0;
        n = (esign > 0) ? n * p : n / p;
    }

    if (e) *e = (char *)s;
    return sign * n;
}

static inline float strtof(const char *s, char **e) {
    return (float)strtod(s, e);
}

static inline long double strtold(const char *s, char **e) {
    return (long double)strtod(s, e);   /* 教学简化: 经 double 计算 */
}

static inline double atof(const char *s) {
    return strtod(s, (char **)0);
}

static unsigned int _seed = 1;

static inline void srand(unsigned int s) {
    _seed = s ? s : 1;
}

static inline int rand(void) {
    _seed ^= _seed << 13;
    _seed ^= _seed >> 17;
    _seed ^= _seed << 5;
    return (int)(_seed & 0x7fffffff);
}


static inline void *realloc(void *p, size_t s) {
    if (!p)   return malloc(s);
    if (s == 0) { free(p); return NULL; }

    usize old_size = malloc_payload_size(p);

    /* 缩小或相等:直接返回原指针（不收缩,合法的 realloc 行为） */
    if (s <= old_size) return p;

    /* 扩大:分配新块,只拷 old_size 字节（绝不越界读） */
    void *np = malloc(s);
    if (!np) return NULL;
    memcpy(np, p, old_size);
    free(p);
    return np;
}



/* bsearch：标准二分查找。要求 base 已按 compar 排好序。
 * 返回值指向**数组内部**的元素（调用方不该 free 它）。
 * 用 lo + (hi - lo) / 2 而不是 (lo + hi) / 2：后者在 nmemb 接近 SIZE_MAX 时溢出。 */
static inline void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
                            int (*compar)(const void *, const void *)) {
    size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void *p = (const char *)base + mid * size;
        int c = compar(key, p);
        if (c == 0) return (void *)p;
        if (c < 0) hi = mid;
        else       lo = mid + 1;
    }
    return (void *)0;
}

static inline void _qswap(char *a, char *b, size_t s) {
    while(s--){char t=*a;*a++=*b;*b++=t;}
}
static inline void _qsort(void *b, size_t n, size_t s, int (*c)(const void*,const void*), size_t th) {
    if(n<=1)return;
    if(n<=th){
        for(size_t i=1;i<n;i++)
            for(size_t j=i;j>0&&c(b+(j-1)*s,b+j*s)>0;j--)
                _qswap((char *)b+(j-1)*s,(char *)b+j*s,s);
        return;
    }
    char *p=(char *)b, *l=(char *)b, *r=(char *)b+(n-1)*s;
    _qswap(p+(n/2)*s,r,s);
    for(;;){
        do l+=s; while(l<=r&&c(l,p)<0);
        do r-=s; while(r>=l&&c(r,p)>0);
        if(l>=r)break;
        _qswap(l,r,s);
    }
    _qswap(p,r,s);
    size_t rn=((char *)b+n*s-r)/s;
    _qsort(b,(r-(char *)b)/s,s,c,th);
    _qsort(r+s,rn-1,s,c,th);
}
static inline void qsort(void *b, size_t n, size_t s, int (*c)(const void*,const void*)) {
    _qsort(b,n,s,c,16);
}

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

#endif