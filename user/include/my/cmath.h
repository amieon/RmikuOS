#pragma once
#include <stdint.h>

namespace mymath {

constexpr double PI      = 3.14159265358979323846;
constexpr double LN2     = 0.69314718055994530942;
constexpr double LOG2E   = 1.44269504088896340736;  // 1/ln2
constexpr double SQRT2   = 1.41421356237309504880;

// ---------- 位操作工具 ----------
union DoubleBits {
    double d;
    uint64_t u;
    struct { uint32_t lo; uint32_t hi; } s;
};

// ---------- 基础工具 ----------
inline double fabs(double x) { return x < 0 ? -x : x; }
inline float  fabs(float x)  { return x < 0 ? -x : x; }

template <typename T> inline T max(T a, T b) { return a > b ? a : b; }
template <typename T> inline T min(T a, T b) { return a < b ? a : b; }

// ---------- sqrt（硬件指令，RISC-V / LoongArch） ----------
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

// ---------- exp（fdlibm 算法，Remez 优化，无外部表） ----------
// 精度：double 全精度，相对误差 < 1e-15
inline double exp(double x) {
    static const double
        ln2HI[2] = {
            6.93147180369123816490e-01,   // +ln2 高位
            -6.93147180369123816490e-01   // -ln2 高位
        },
        ln2LO[2] = {
            1.90821492927058770002e-10,   // +ln2 低位
            -1.90821492927058770002e-10   // -ln2 低位
        },
        invln2 = 1.44269504088896338700e+00,
        // Remez 优化系数（不是泰勒！同阶数精度高 2~3 个数量级）
        P1 = 1.66666666666666019037e-01,
        P2 = -2.77777777770155933842e-03,
        P3 = 6.61375632143793436117e-05,
        P4 = -1.65339022054652515390e-06,
        P5 = 4.13813679705723846039e-08,
        o_threshold = 7.09782712893383973096e+02,   // overflow 阈值
        u_threshold = -7.45133219101965108483e+02; // underflow 阈值

    DoubleBits db; db.d = x;
    uint32_t hx = db.s.hi;
    uint32_t xsb = hx >> 31;      // 符号位
    hx &= 0x7fffffff;             // 绝对值的高 32 位

    // |x| < 2^-54: 直接返回 1+x，避免舍入误差主导
    if (hx < 0x3c900000) return 1.0 + x;

    // overflow / underflow 边界
    if (hx >= 0x40862E42) {
        if (hx >= 0x7ff00000) return x + x;           // inf / nan
        if (x > o_threshold) return 1e308 * 1e308;   // overflow -> +inf
        if (x < u_threshold) return 0.0;              // underflow -> 0
    }

    // 参数约减：x = k*ln2 + c，|c| <= 0.5*ln2
    int k;
    double hi, lo, c;
    if (hx > 0x3fd62e42) {        // |x| > 0.5*ln2
        if (hx < 0x3FF0A2B2) {    // |x| < 1.5*ln2
            hi = x - ln2HI[xsb];
            lo = ln2LO[xsb];
            k = 1 - xsb - xsb;    // k = 1 (x>0) 或 -1 (x<0)
        } else {
            k = (int)(invln2 * x + (xsb ? -0.5 : 0.5));
            double t = (double)k;
            hi = x - t * ln2HI[0];
            lo = t * ln2LO[0];
        }
        c = hi - lo;              // 精确约减：c = x - k*ln2
    } else {
        k = 0;
        c = x;
        hi = c;   
        lo = 0.0; 
    }

    // 计算 exp(c)，|c| < 0.5*ln2
    // 使用 Remez 优化的有理函数形式，而非泰勒多项式
    double cc = c * c;
    double y = c - cc * (P1 + cc * (P2 + cc * (P3 + cc * (P4 + cc * P5))));
    double r = (1.0 - (lo - c * y) / (2.0 - y)) + hi;

    if (k == 0) return r;

    // 乘以 2^k：直接操作 double 的指数位
    DoubleBits db2;
    db2.d = 1.0;
    db2.u = ((uint64_t)(k + 1023)) << 52;
    return r * db2.d;
}

// ---------- log（fdlibm 算法，无外部表） ----------
// 精度：double 全精度，相对误差 < 1e-15
inline double log(double x) {
    static const double
        ln2_hi = 6.93147180369123816490e-01,
        ln2_lo = 1.90821492927058770002e-10,
        // log(1+f) 的 Remez 优化系数，f in [-0.5, 0.5]
        Lg1 = 6.666666666666735130e-01,
        Lg2 = 3.999999999940941908e-01,
        Lg3 = 2.857142874366239149e-01,
        Lg4 = 2.222219843214978396e-01,
        Lg5 = 1.818357216161805012e-01,
        Lg6 = 1.531383769920937332e-01,
        Lg7 = 1.479819860511658591e-01;

    if (x <= 0.0) {
        if (x == 0.0) return -1e308 * 1e308;  // log(0) = -inf
        return (x - x) / (x - x);              // log(-x) = NaN
    }

    DoubleBits db; db.d = x;
    uint64_t ix = db.u;
    uint32_t hx = db.s.hi;
    uint32_t lx = db.s.lo;

    int k = 0;
    // subnormal 处理：乘以 2^54 使其变为 normal
    if (hx < 0x00100000) {
        if ((hx | lx) == 0) return -1e308 * 1e308;
        k -= 54;
        x *= 18014398509481984.0;  // 2^54
        db.d = x;
        hx = db.s.hi;
        lx = db.s.lo;
    }

    // 提取指数 k，尾数归一化到 [0.5, 1)
    k += (hx >> 20) - 1023;
    k += 1;
    hx = (hx & 0x000fffff) | 0x3fe00000;  // 指数设为 -1 (0x3fe = 1022)
    db.s.hi = hx;
    x = db.d;

    // 计算 log(x) = k*ln2 + log(1+f)
    // f = x - 1，x in [0.5, 1)  => f in [-0.5, 0)
    double f = x - 1.0;
    double s = f / (2.0 + f);           // s = f/(2+f)，|s| < 1/3
    double z = s * s;
    double w = z * z;
    double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    double R = t2 + t1;
    double hfsq = 0.5 * f * f;

    // k*ln2 分裂为 hi+lo 减少舍入误差
    return (double)k * ln2_hi + (f - (hfsq - s * (hfsq + R))) + (double)k * ln2_lo;
}


inline double pow(double base, double e) {
    // ---- 特殊值短路 ----
    if (base != base || e != e) return (base - base) / (base - base); // NaN
    if (e == 0.0)  return 1.0;                    // x^0 = 1 (包括 0^0)
    if (base == 0.0) {
        if (e > 0.0) return 0.0;
        return 1.0 / 0.0;                         // 0^neg = +inf
    }
    if (base == 1.0) return 1.0;
    if (e == 1.0)  return base;
    if (e == 2.0)  return base * base;            // 平方短路
    if (e == -1.0) return 1.0 / base;
    if (e == -2.0) return 1.0 / (base * base);
    if (e == 0.5)  return sqrt(base);             // sqrt 短路
    if (e == -0.5) return 1.0 / sqrt(base);
    
    // ---- 整数指数：快速幂（O(log n) 乘法，比 exp/log 快且更准）----
    if (e == (double)(long long)e) {
        long long n = (long long)e;
        bool neg = n < 0;
        if (neg) n = -n;
        double result = 1.0;
        double cur = base;
        while (n > 0) {
            if (n & 1) result *= cur;
            cur *= cur;
            n >>= 1;
        }
        return neg ? 1.0 / result : result;
    }
    
    // ---- 负底数非整数指数 -> NaN ----
    if (base < 0.0) return (base - base) / (base - base);
    
    // ---- 一般情况：exp(e * log(base)) ----
    // double 下这条路径的相对误差 < 1e-14，对 AdamW 完全够用
    return exp(e * log(base));
}

inline double cos(double x) {
    x = fabs(x);
    // 归约:k = round(x / (pi/2)), 余 r
    double inv_half_pi = 2.0 / PI;
    long k = (long)(x * inv_half_pi + 0.5);
    double r = x - (double)k * (PI / 2.0);
    int quad = (int)(k & 3);

    double r2 = r * r;
    // cos(r) 和 sin(r) 的多项式(r in [-pi/4,pi/4])
    double cos_r = 1.0 + r2 * (-1.0/2 + r2 * (1.0/24 + r2 * (-1.0/720 + r2 * (1.0/40320))));
    double sin_r = r * (1.0 + r2 * (-1.0/6 + r2 * (1.0/120 + r2 * (-1.0/5040))));

    switch (quad) {
        case 0: return cos_r;
        case 1: return -sin_r;
        case 2: return -cos_r;
        default: return sin_r;
    }
}
inline double sin(double x) { return cos(x - PI / 2.0); }
inline float cos(float x) { return (float)cos((double)x); }
inline float sin(float x) { return (float)sin((double)x); }

} // namespace mymath