#pragma once
#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>
#include "mem.h"
#include "io.h"
#include "process.h"
#include "string.h"
#include "stdio.h"


/* ---- misc (inline) ---- */
static inline void abort(void) {
    printf("abort\n");
    exit(127);
}

static inline char *getenv(const char *name) { (void)name; return (char*)0; }

static inline int abs(int x) {
    return x < 0 ? -x : x;
}

static inline long strtol(const char *s, char **e, int b) {
    long n = 0;
    int sign = 1;

    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    if (*s == '-')      { sign = -1; s++; }
    else if (*s == '+') { s++; }

    /* auto-detect base */
    if (b == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') { b = 16; s++; }
            else                         { b = 8; }
        } else {
            b = 10;
        }
    } else if (b == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    }

    for (;;) {
        int d;
        if      (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= b) break;
        n = n * b + d;
        s++;
    }
    if (e) *e = (char *)s;
    return sign * n;
}

static inline unsigned long strtoul(const char *s, char **e, int b) {
    return (unsigned long)strtol(s, e, b);
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
    void *np = malloc(s);
    if (!np) return NULL;
    memcpy(np, p, s);   /* 注意：如果 s > 旧大小会越界读，按需改 */
    free(p);
    return np;
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