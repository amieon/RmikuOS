#pragma once
#include <stdint.h>

namespace mymath {

constexpr double PI = 3.14159265358979323846;


union DoubleBits {
    double d;
    uint64_t u;
    struct { uint32_t lo; uint32_t hi; } s;
};


inline double exp(double x) {
    static const double
        ln2HI[2] = {
            6.93147180369123816490e-01,  
            -6.93147180369123816490e-01   
        },
        ln2LO[2] = {
            1.90821492927058770002e-10,
            -1.90821492927058770002e-10   
        },
        invln2 = 1.44269504088896338700e+00;


    static const double
        P1 = 1.66666666666666019037e-01,
        P2 = -2.77777777770155933842e-03,
        P3 = 6.61375632143793436117e-05,
        P4 = -1.65339022054652515390e-06,
        P5 = 4.13813679705723846039e-08;


    if (x > 709.0)  return 1e308 * 1e308;  
    if (x < -745.0) return 0.0;            

    DoubleBits db; db.d = x;
    uint32_t hx = db.s.hi;
    uint32_t xabs = hx & 0x7fffffff;


    if (xabs < 0x3e300000) return 1.0 + x;


    int sign = hx >> 31;
    int k;
    double c, hi, lo;

    if (xabs > 0x3fd62e42) {       
        if (xabs < 0x3FF0A2B2) {      
            hi = x - ln2HI[sign];
            lo = ln2LO[sign];
            k = 1 - sign - sign;    
        } else {
            k = (int)(x * invln2 + (sign ? -0.5 : 0.5));
            hi = x - k * ln2HI[sign];
            lo = k * ln2LO[sign];
        }
        c = hi - lo;                
    } else {
        k = 0;
        c = x;
        hi = x;
        lo = 0;
    }


    double xx = c * c;
    double y = c - xx * (P1 + xx * (P2 + xx * (P3 + xx * (P4 + xx * P5))));
    double r = (1.0 - (lo - c * y) / (2.0 - y)) + hi;

    if (k == 0) return r;

    DoubleBits db2;
    db2.d = 1.0;
    db2.u = ((uint64_t)(k + 1023)) << 52;
    return r * db2.d;
}

inline double log(double x) {
    static const double
        ln2_hi = 6.93147180369123816490e-01,
        ln2_lo = 1.90821492927058770002e-10,
        Lg1 = 6.666666666666735130e-01,
        Lg2 = 3.999999999940941908e-01,
        Lg3 = 2.857142874366239149e-01,
        Lg4 = 2.222219843214978396e-01,
        Lg5 = 1.818357216161805012e-01,
        Lg6 = 1.531383769920937332e-01,
        Lg7 = 1.479819860511658591e-01;

    if (x <= 0.0) {
        if (x == 0.0) return -1e308 * 1e308;  
        return (x - x) / (x - x);           
    }

    DoubleBits db; db.d = x;
    uint32_t hx = db.s.hi;
    uint32_t lx = db.s.lo;
    uint32_t ix = hx & 0x7fffffff;

    int k = 0;

    if (ix < 0x00100000) {
        if ((ix | lx) == 0) return -1e308 * 1e308;
        k -= 54;
        x *= 18014398509481984.0; 
        db.d = x;
        hx = db.s.hi;
    }


    k += (hx >> 20) - 1023;
    hx = (hx & 0x000fffff) | 0x3fe00000;  
    db.s.hi = hx;
    x = db.d;

    double f = x - 1.0;                 
    double s = f / (2.0 + f);
    double z = s * s;
    double w = z * z;
    double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    double R = t2 + t1;
    double hfsq = 0.5 * f * f;

    return (double)k * ln2_hi + (f - (hfsq - s * (hfsq + R))) + (double)k * ln2_lo;
}


inline double sqrt(double x) {
    double r;
#if defined(__riscv)
    __asm__ volatile("fsqrt.d %0, %1" : "=f"(r) : "f"(x));
#elif defined(__loongarch__) || defined(__loongarch64__)
    __asm__ volatile("fsqrt.d %0, %1" : "=f"(r) : "f"(x));
#else
    // 兜底：牛顿迭代（主机测试用）
    if (x <= 0) return 0;
    r = x;
    for (int i = 0; i < 60; ++i) r = 0.5 * (r + x / r);
#endif
    return r;
}


inline double pow(double base, double e) {
    if (base == 0.0) return 0.0;
    if (e == 0.0)    return 1.0;
    if (base < 0.0)  return (base - base) / (base - base);
    return exp(e * log(base));
}


inline double fabs(double x) { return x < 0 ? -x : x; }

inline float  fabs(float x)  { return x < 0 ? -x : x; }

template <typename T> inline T max(T a, T b) { return a > b ? a : b; }
template <typename T> inline T min(T a, T b) { return a < b ? a : b; }

} // namespace mymath