#include "mem.h"
#include "my/cmath.h"

extern "C" {
    long syscall3(unsigned long, unsigned long, unsigned long, unsigned long);
}
static void puts_raw(const char* s){unsigned long n=0;while(s[n])n++;syscall3(2,1,(unsigned long)s,n);}

// 打印 double:整数部分 . 小数前6位(简易,够看精度)
static void put_double(double v) {
    if (v < 0) { puts_raw("-"); v = -v; }
    long ip = (long)v;
    double frac = v - (double)ip;
    // 整数部分
    char b[24]; int n=0;
    if (ip==0) b[n++]='0';
    char t[24]; int k=0; long x=ip;
    while(x>0){t[k++]=char('0'+x%10);x/=10;}
    while(k>0)b[n++]=t[--k];
    b[n++]='.'; 
    // 小数 6 位
    for (int d=0; d<6; d++) {
        frac *= 10;
        int digit = (int)frac;
        b[n++] = char('0' + digit);
        frac -= digit;
    }
    b[n++]='\n'; b[n]=0;
    puts_raw(b);
}

extern "C" int main() {
    using namespace mymath;
    puts_raw("== cmath test ==\n");

    // sqrt
    puts_raw("sqrt(4)    = "); put_double(sqrt(4.0));      // 2.000000
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
    syscall3(0,0,0,0);
    return 0;
}