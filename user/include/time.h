#pragma once
#ifndef _TIME_H
#define _TIME_H
#include "arch.h"
typedef long time_t;
typedef long clock_t;

/* 返回自 epoch 以来的秒数，tloc 可为 NULL */
typedef long clock_t;
typedef long time_t;

#define CLOCKS_PER_SEC 1000000L

static inline clock_t clock(void) {
    return (clock_t)get_ticks();
}

static inline time_t time(time_t *t) {
    time_t v = (time_t)get_time();
    if (t) *t = v;
    return v;
}

/* 后面 loslib.c 会需要，先占位 */
typedef long suseconds_t;

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


#endif