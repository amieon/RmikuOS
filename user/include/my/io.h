

#pragma once
#include "../include/syscall.h"  
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

namespace io {

// 字符串长度
inline unsigned long cstr_len(const char* s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

// 打印字符串(fd=1 标准输出)
inline void puts(const char* s) {
    syscall3(SYS_WRITE, 1, (unsigned long)s, cstr_len(s));
}


inline void put_int(long v, bool newline = true) {
    char buf[24]; int n = 0;
    if (v == 0) buf[n++] = '0';
    int neg = v < 0; if (neg) v = -v;
    char tmp[24]; int t = 0;
    while (v > 0) { tmp[t++] = char('0' + v % 10); v /= 10; }
    if (neg) buf[n++] = '-';
    while (t > 0) buf[n++] = tmp[--t];
    if (newline) buf[n++] = '\n';
    buf[n] = 0;
    puts(buf);
}


inline void put_double(double v, int prec = 12, bool newline = true) {
    char buf[256];
    int n = 0;


    if (v != v) {
        buf[n++] = 'n'; buf[n++] = 'a'; buf[n++] = 'n';
        if (newline) buf[n++] = '\n';
        buf[n] = '\0';
        puts(buf);
        return;
    }

    if (v > 1e308 || v < -1e308) {
        if (v < 0) buf[n++] = '-';
        buf[n++] = 'i'; buf[n++] = 'n'; buf[n++] = 'f';
        if (newline) buf[n++] = '\n';
        buf[n] = '\0';
        puts(buf);
        return;
    }

    if (v < 0) { buf[n++] = '-'; v = -v; }

    long ip = (long)v;
    double frac = v - (double)ip;


    if (ip == 0) {
        buf[n++] = '0';
    } else {
        char tmp[64];
        int t = 0;
        long x = ip;
        while (x > 0) { tmp[t++] = char('0' + x % 10); x /= 10; }
        while (t > 0) buf[n++] = tmp[--t];
    }

    buf[n++] = '.';


    for (int d = 0; d < prec; d++) {
        frac *= 10.0;
        int digit = (int)frac;
        if (digit < 0) digit = 0;
        if (digit > 9) digit = 9;
        buf[n++] = char('0' + digit);
        frac -= digit;
    }

    if (newline) buf[n++] = '\n';
    buf[n] = '\0'; 
    puts(buf);
}
// 退出
inline void exit(int code) {
    syscall3(SYS_EXIT, (unsigned long)code, 0, 0);
}


} // namespace io