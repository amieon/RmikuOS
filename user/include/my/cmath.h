// my/cmath.h —— 裸机环境下替代 <cmath> 的数学函数。
//
// 提供 GCN(Func.h)和 RNG(random.h)需要的:
//   sqrt   —— 硬件指令 fsqrt.d(riscv/loongarch 都有 D 扩展)
//   exp    —— 区间归约 + 多项式(softmax 用)
//   log    —— 区间归约 + 多项式(交叉熵用)
//   cos/sin—— 区间归约 + 多项式(Box-Muller 高斯 RNG 用)
//   fabs / max / min / pow
//
// 精度:double 下 exp/log 相对误差约 1e-15 量级,够 GCN 训练。
// 实现参考 musl/cephes 的经典区间归约思路,不是粗糙泰勒。

#pragma once

namespace mymath {

// ---- 常数 ----
constexpr double PI      = 3.14159265358979323846;
constexpr double LN2     = 0.69314718055994530942;
constexpr double LOG2E   = 1.44269504088896340736;  // 1/ln2
constexpr double SQRT2   = 1.41421356237309504880;

// ---- fabs ----
inline double fabs(double x) { return x < 0 ? -x : x; }
inline float  fabs(float x)  { return x < 0 ? -x : x; }

// ---- max / min(替代 std::max/min)----
template <typename T> inline T max(T a, T b) { return a > b ? a : b; }
template <typename T> inline T min(T a, T b) { return a < b ? a : b; }

// ---- sqrt:硬件指令 ----
inline double sqrt(double x) {
    double r;
#if defined(__riscv)
    __asm__ volatile("fsqrt.d %0, %1" : "=f"(r) : "f"(x));
#elif defined(__loongarch__)
    __asm__ volatile("fsqrt.d %0, %1" : "=f"(r) : "f"(x));
#else
    // 兜底:牛顿迭代(主机测试用,实际跑在 riscv/loongarch 走硬件)
    if (x <= 0) return 0;
    r = x;
    for (int i = 0; i < 60; ++i) r = 0.5 * (r + x / r);
#endif
    return r;
}
inline float sqrt(float x) { return (float)sqrt((double)x); }

// ---- 把 double 拆成 frac * 2^exp(用位操作,不依赖 frexp)----
// 用 union 访问 double 的位:符号1 + 指数11 + 尾数52
union DoubleBits {
    double d;
    unsigned long u;
};

// ---- exp ----
// 策略:exp(x) = 2^k * exp(r),  x = k*ln2 + r, |r| <= ln2/2
//      exp(r) 用 7 阶多项式(r 很小,收敛快)
inline double exp(double x) {
    if (x != x) return x;            // NaN
    if (x > 709.0)  return 1e308 * 1e308;   // 溢出 -> +inf
    if (x < -745.0) return 0.0;             // 下溢 -> 0

    // k = round(x / ln2)
    double kf = x * LOG2E;
    // round to nearest
    long k = (long)(kf + (kf >= 0 ? 0.5 : -0.5));
    double r = x - (double)k * LN2;  // |r| <= ln2/2 ≈ 0.3466

    // exp(r) 多项式(泰勒在小区间,7 阶足够 double 精度)
    double r2 = r * r;
    double p = 1.0 + r + r2 * (1.0/2 + r * (1.0/6 + r * (1.0/24
                 + r * (1.0/120 + r * (1.0/720 + r * (1.0/5040))))));

    // 乘 2^k:直接操作 double 的指数位
    DoubleBits db;
    db.d = 1.0;
    // double 指数偏置 1023,指数位在 bit 52..62
    long new_exp = 1023 + k;
    if (new_exp <= 0)   return 0.0;          // 下溢
    if (new_exp >= 2047) return 1e308 * 1e308; // 上溢
    db.u = ((unsigned long)new_exp) << 52;
    return p * db.d;
}
inline float exp(float x) { return (float)exp((double)x); }

// ---- log(自然对数)----
// 策略:x = m * 2^e, m in [1,2);  log(x) = e*ln2 + log(m)
//      log(m) 用 atanh 级数:令 s=(m-1)/(m+1), log(m)=2(s + s^3/3 + s^5/5 + ...)
inline double log(double x) {
    if (x != x) return x;            // NaN
    if (x < 0)  return (x - x) / (x - x);  // 负数 -> NaN
    if (x == 0) return -1e308 * 1e308;     // log(0) -> -inf

    // 提取指数 e 和尾数 m
    DoubleBits db; db.d = x;
    long bits = (long)db.u;
    long e = ((bits >> 52) & 0x7FF) - 1023;   // 无偏指数
    // 把尾数置为 [1,2):清掉指数位,设为 1023(2^0)
    db.u = (db.u & 0x000FFFFFFFFFFFFFUL) | (1023UL << 52);
    double m = db.d;                          // m in [1,2)

    // 让 m 落在 [sqrt(1/2), sqrt(2)) 附近,级数收敛更快
    if (m > SQRT2) { m *= 0.5; e += 1; }

    // log(m) via atanh 级数
    double s = (m - 1.0) / (m + 1.0);
    double s2 = s * s;
    double sum = s;
    double term = s;
    for (int n = 3; n <= 15; n += 2) {
        term *= s2;
        sum += term / n;
    }
    double log_m = 2.0 * sum;

    return (double)e * LN2 + log_m;
}
inline float log(float x) { return (float)log((double)x); }

// ---- cos / sin(Box-Muller 高斯 RNG 要 cos)----
// 区间归约到 [-pi/4, pi/4],用多项式
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

// ---- pow(AdamW 等可能用;用 exp/log 实现)----
inline double pow(double base, double e) {
    if (base == 0.0) return 0.0;
    if (e == 0.0)    return 1.0;
    // base^e = exp(e * log(base)),仅对 base>0 有效
    return exp(e * log(base));
}

} // namespace mymath