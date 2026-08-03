#pragma once
#ifndef _TIME_H
#define _TIME_H

#include "arch.h"
#include "types.h"
#include <stdio.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef _RMIKU_TIME_T_DEFINED
#define _RMIKU_TIME_T_DEFINED
typedef long time_t;             /* 纪元秒（自 1970-01-01 起）*/
#endif

#ifndef _RMIKU_USECONDS_T_DEFINED
#define _RMIKU_USECONDS_T_DEFINED
typedef unsigned int useconds_t; /* usleep() 的微秒参数 */
#endif

#ifndef _RMIKU_CLOCK_T_DEFINED
#define _RMIKU_CLOCK_T_DEFINED
typedef long clock_t;             /* 进程时间计数类型 */
#endif

#ifndef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 1000000L   /* clock() 每秒的计数；本 OS 仅作熵源，精确值无影响 */
#endif

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

struct timeval {
    time_t tv_sec;
    long   tv_usec;
};

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

typedef struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
} timezone;

typedef int clockid_t;


static inline time_t time(time_t *t) {
    /* 墙钟秒(epoch): 校准(ntpdate)后为真值, 否则 0 */
    time_t now = (time_t)get_epoch();
    if (t) *t = now;
    return now;
}

/* 进程已用 CPU 时间。本 OS 无 per-process 计时，用单调时钟计数作熵源，
 * 满足 lua 等把 clock() 当随机种子的用途。 */
static inline clock_t clock(void) {
    return (clock_t)get_time();
}

static inline int gettimeofday(struct timeval *tv, struct timezone *tz) {
    time_t now = (time_t)get_epoch();
    if (tv) { tv->tv_sec = now; tv->tv_usec = 0; }
    if (tz) { tz->tz_minuteswest = 0; tz->tz_dsttime = 0; }
    return 0;
}

static inline int clock_gettime(clockid_t clk, struct timespec *ts) {
    (void)clk;
    time_t now = (time_t)get_time();
    if (ts) { ts->tv_sec = now; ts->tv_nsec = 0; }
    return 0;
}

static inline int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req) return 0;
    long end = (long)get_time() + (long)req->tv_sec;
    while ((long)get_time() < end) { /* busy spin */ }
    return 0;
}

static inline int usleep(useconds_t us) {
    long end = (long)get_time() + (long)(us / 1000000UL);
    while ((long)get_time() < end) { /* busy spin */ }
    return 0;
}

static inline int is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static inline struct tm *gmtime_r(const time_t *t, struct tm *res) {
    long long tt = (long long)*t;
    long long days = tt / 86400LL;
    int rem = (int)(tt % 86400LL);
    int hour = rem / 3600;
    int minute = (rem % 3600) / 60;
    int second = rem % 60;
    int wday = (int)((4LL + days) % 7LL);
    long long z = days + 719468LL;
    long long era = (z >= 0 ? z : z - 146096LL) / 146097LL;
    long long doe = z - era * 146097LL;                              /* [0,146096] */
    long long yoe = (doe - doe / 1460LL + doe / 36524LL - doe / 146096LL) / 365LL;  /* [0,399] */
    long long y = yoe + era * 400LL;
    long long doy = doe - (365LL * yoe + yoe / 4 - yoe / 100);       /* [0,365] */
    long long mp = (5 * doy + 2) / 153;                              /* [0,11] */
    int mday = (int)(doy - (153 * mp + 2) / 5 + 1);                  /* [1,31] */
    int month = (int)(mp < 10 ? mp + 3 : mp - 9);                    /* [1,12] */
    /* Hinnant 返回"调整年"(1/2 月  上一年): m<=2 时公历年 = y+1 */
    if (month <= 2) y += 1;
    res->tm_sec = second;
    res->tm_min = minute;
    res->tm_hour = hour;
    res->tm_mday = mday;
    res->tm_mon = month - 1;
    res->tm_year = (int)y - 1900;
    res->tm_wday = wday;
    res->tm_yday = (int)doy;
    res->tm_isdst = 0;
    return res;
}

static inline struct tm *gmtime(const time_t *t) {
    static struct tm _g;
    return gmtime_r(t, &_g);
}

static inline struct tm *localtime_r(const time_t *t, struct tm *res) {
    return gmtime_r(t, res);
}

static inline struct tm *localtime(const time_t *t) {
    static struct tm _l;
    return localtime_r(t, &_l);
}

static inline time_t mktime(struct tm *tm) {
    static const int days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    int year = tm->tm_year + 1900;
    int leap = is_leap(year);
    int y = year - 1;
    long long days = (long long)y * 365LL + y / 4 - y / 100 + y / 400;
    if (tm->tm_mon > 2) days += leap;
    days += days_before_month[tm->tm_mon];
    days += tm->tm_mday - 1;
    return (time_t)((days * 86400LL) + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
}

static inline double difftime(time_t a, time_t b) {
    return (double)(a - b);
}

static inline size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm) {
    if (!s || !fmt) return 0;
    size_t i = 0;
    for (; *fmt && i < max - 1; ++fmt) {
        if (*fmt != '%') { s[i++] = *fmt; continue; }
        switch (*++fmt) {
            case 'Y': i += (size_t)snprintf(s + i, max - i, "%04d", tm->tm_year + 1900); break;
            case 'y': i += (size_t)snprintf(s + i, max - i, "%02d", tm->tm_year % 100); break;
            case 'm': i += (size_t)snprintf(s + i, max - i, "%02d", tm->tm_mon + 1); break;
            case 'd': i += (size_t)snprintf(s + i, max - i, "%02d", tm->tm_mday); break;
            case 'H': i += (size_t)snprintf(s + i, max - i, "%02d", tm->tm_hour); break;
            case 'M': i += (size_t)snprintf(s + i, max - i, "%02d", tm->tm_min); break;
            case 'S': i += (size_t)snprintf(s + i, max - i, "%02d", tm->tm_sec); break;
            case 'w': i += (size_t)snprintf(s + i, max - i, "%d", tm->tm_wday); break;
            default: break;
        }
    }
    s[i] = 0;
    return i;
}

static inline char *asctime(const struct tm *tm) {
    static char buf[26];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    return buf;
}

static inline char *ctime(const time_t *t) {
    static char buf[26];
    struct tm *tm = localtime(t);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    return buf;
}

#endif /* _TIME_H */
