#include "mem.h"
#include "cmath"


#pragma once
#include "../include/syscall.h"  
#include "stdio.h"
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

static void puts_raw(const char* s){unsigned long n=0;while(s[n])n++;syscall3(2,1,(unsigned long)s,n);}
using namespace io;

extern "C" int main() {
    using namespace mymath;
    puts_raw("== cmath test ==\n");

    // sqrt
    puts_raw("sqrt(4)    = "); put_double(0.0);  puts_raw("flag") ;    // 2.000000
    puts_raw("sqrt(2)    = "); put_double(sqrt(2.0));      // 1.414213
    puts_raw("sqrt(100)  = "); put_double(sqrt(100.0));    // 10.000000

    // exp
    puts_raw("exp(0)     = "); put_double(exp(0.0));       // 1.000000
    puts_raw("exp(1)     = "); put_double(exp(1.0));       // 2.718281
    puts_raw("exp(2)     = "); put_double(exp(2.0));       // 7.389056
    puts_raw("exp(-1)    = "); put_double(exp(-1.0));      // 0.367879

    // log
    puts_raw("log(1)     = "); put_double(log(1.0));       // 0.000000
    puts_raw("log(e)     = "); put_double(log(2.718281828)); // 0.999999
    puts_raw("log(10)    = "); put_double(log(10.0));      // 2.302585
    puts_raw("log(0.5)   = "); put_double(log(0.5));       // -0.693147

    // cos
    puts_raw("cos(0)     = "); put_double(cos(0.0));       // 1.000000
    puts_raw("cos(pi/3)  = "); put_double(cos(PI/3.0));    // 0.500000
    puts_raw("cos(pi/2)  = "); put_double(cos(PI/2.0));    // 0.000000(接近)
    puts_raw("cos(pi)    = "); put_double(cos(PI));        // -1.000000

    // exp/log 往返(softmax/CE 关键):log(exp(x)) 应 = x
    puts_raw("log(exp(3))= "); put_double(log(exp(3.0)));  // 3.000000

    // pow
    puts_raw("pow(2,10)  = "); put_double(pow(2.0, 10.0)); // 1024.000000

    puts_raw("== done ==\n");
    
    return 0;
}