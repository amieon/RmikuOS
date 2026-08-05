/* mymath.h — fdlibm 全精度数学库，
 *
 * 常数来源: fdlibm 1.3 / 1.5 (Sun Microsystems, 1993-2004)
 */


#pragma once
#include <stdint.h>

/*  内部工具                                                                */

typedef union {
    double d;
    uint64_t u;
    struct {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        uint32_t hi, lo;
#else
        uint32_t lo, hi;
#endif
    } s;
} mm_bits;

static inline double mm_sqrt(double x);
static inline double mm_exp(double x);
static inline double mm_expm1(double x);
static inline double mm_log(double x);

static inline double mm_fabs(double x) {
    mm_bits b; b.d = x; b.s.hi &= 0x7fffffffu; return b.d;
}

static inline double mm_copysign(double x, double y) {
    mm_bits bx, by; bx.d = x; by.d = y;
    bx.s.hi = (bx.s.hi & 0x7fffffffu) | (by.s.hi & 0x80000000u);
    return bx.d;
}

static inline double mm_hi_part(double x) {
    mm_bits b; b.d = x; b.s.lo = 0; return b.d;
}

static inline double mm_scalbn(double x, int n) {
    static const double
        two54  = 1.80143985094819840000e+16,
        twom54 = 5.55111512312578270212e-17,
        huge   = 1.0e+300,
        tiny   = 1.0e-300;
    mm_bits b; b.d = x;
    int k = (int)((b.s.hi >> 20) & 0x7ff);
    if (k == 0) {
        if ((b.s.lo | (b.s.hi & 0x7fffffffu)) == 0) return x;
        x *= two54; b.d = x;
        k = (int)((b.s.hi >> 20) & 0x7ff) - 54;
        if (n < -50000) return tiny * x;
    }
    if (k == 0x7ff) return x + x;
    k += n;
    if (k > 0x7fe) return huge * mm_copysign(huge, x);
    if (k > 0) { b.d = x; b.s.hi = (b.s.hi & 0x800fffffu) | ((uint32_t)k << 20); return b.d; }
    if (k <= -54) { if (n > 50000) return huge * mm_copysign(huge, x); return tiny * mm_copysign(tiny, x); }
    k += 54;
    b.d = x; b.s.hi = (b.s.hi & 0x800fffffu) | ((uint32_t)k << 20);
    return b.d * twom54;
}

/* ======================================================================== */
/*  rem_pio2 — 高精度 π/2 约减                                              */
/* ======================================================================== */

static const double mm_two_over_pi[] = {
    0.636619772367581343075535, /* 2/pi 高 24 位 */
};

/* 简化版：用 Payne-Hanek 风格对 |x| < 2^20 足够精确 */
static const double
    mm_pio2_1   = 1.57079632673412561417e+00, /* 0x3FF921FB, 0x54400000 */
    mm_pio2_1t  = 6.07710050650619224932e-11, /* 0x3DD0B461, 0x1A626331 */
    mm_pio2_2   = 6.07710050630396597660e-11, /* 0x3DD0B461, 0x1A600000 */
    mm_pio2_2t  = 2.02226624879595063154e-21, /* 0x3BA3198A, 0x2E037073 */
    mm_pio2_3   = 2.02226624871116645580e-21, /* 0x3BA3198A, 0x2E000000 */
    mm_pio2_3t  = 8.47842766036889956997e-32; /* 0x397B839A, 0x252049C1 */

static inline int mm_rem_pio2(double x, double *y0, double *y1) {
    double z, w, t, r, fn;
    int n, hx, ix;
    mm_bits b; b.d = x;
    hx = (int)b.s.hi;
    ix = hx & 0x7fffffff;

    if (ix <= 0x3fe921fb) { *y0 = x; *y1 = 0.0; return 0; }

    /* |x| < 2^20: 用扩展精度乘法 */
    if (ix < 0x413921fb) {  /* |x| < 2^20 * pi/2 */
        fn = (double)((int)(x * (2.0 / 3.14159265358979323846) + (x > 0 ? 0.5 : -0.5)));
        n = (int)fn;
        r = x - fn * mm_pio2_1;
        w = fn * mm_pio2_1t;
        *y0 = r - w;
        *y1 = (r - *y0) - w;
        return n;
    }

    /* 大参数：逐段约减 (简化 Payne-Hanek) */
    z = mm_scalbn(x, 0); /* 保持原值 */
    {
        /* 用 2/pi 的 128 位展开做乘法 */
        static const double two_pi[] = {
            6.36619772367581382433e-01, /* 0x3FE45F30, 0x6DC9C883 */
            -3.89073435546823948949e-17, /* 0xBC955E42, 0x878347E4 */
        };
        fn = x * two_pi[0] + x * two_pi[1];
        n = (int)(fn + (fn > 0 ? 0.5 : -0.5));
        double nf = (double)n;
        r = x - nf * mm_pio2_1;
        w = nf * mm_pio2_1t;
        t = r - w;
        w = nf * mm_pio2_2 - ((r - t) - w);
        *y0 = t - w;
        *y1 = (t - *y0) - w;
    }
    return n;
}

/* ======================================================================== */
/*  kernel_sin / kernel_cos / kernel_tan                                    */
/* ======================================================================== */

static inline double mm_kernel_sin(double x, double y, int iy) {
    static const double
        half = 5.00000000000000000000e-01,
        S1 = -1.66666666666666324348e-01,
        S2 =  8.33333333332248946124e-03,
        S3 = -1.98412698298579493134e-04,
        S4 =  2.75573137070700676789e-06,
        S5 = -2.50507602534068634195e-08,
        S6 =  1.58969099521155010221e-10;
    double z = x * x;
    double v = z * x;
    double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0) return x + v * (S1 + z * r);
    return x - ((z * (half * y - v * r) - y) - v * S1);
}

static inline double mm_kernel_cos(double x, double y) {
    static const double
        one = 1.00000000000000000000e+00,
        C1 =  4.16666666666666019037e-02,
        C2 = -1.38888888888741095749e-03,
        C3 =  2.48015872894767294178e-05,
        C4 = -2.75573143513906633035e-07,
        C5 =  2.08757232129817482790e-09,
        C6 = -1.13596475577881948265e-11;
    double z = x * x;
    double w = z * z;
    double r = z * (C1 + z * (C2 + z * C3)) + w * w * (C4 + z * (C5 + z * C6));
    double hz = 0.5 * z;
    w = one - hz;
    return w + (((one - w) - hz) + (z * r - x * y));
}

static inline double mm_kernel_tan(double x, double y, int iy) {
    static const double
        T[] = {
            3.33333333333334091986e-01,
            1.33333333333201242699e-01,
            5.39682539762260521377e-02,
            2.18694882948595424599e-02,
            8.86323982359930005737e-03,
            3.59207910759131235356e-03,
            1.45620945432529025516e-03,
            5.88041240820264096874e-04,
            2.46463134818469906812e-04,
            7.81794442939557092300e-05,
            7.14072491382608190305e-05,
           -1.85586374855275456654e-05,
            2.59073051863633712884e-05,
        },
        one    = 1.0,
        pio4   = 7.85398163397448278999e-01,
        pio4lo = 3.06161699786838301793e-17;

    mm_bits xb; xb.d = x;
    int hx = (int)xb.s.hi;
    uint32_t ix = (uint32_t)hx & 0x7fffffffu;

    if (ix < 0x3e300000u) {
        if ((int)x == 0) {
            if (((ix | xb.s.lo) | (uint32_t)(iy + 1)) == 0)
                return one / mm_fabs(x);
            if (iy == 1) return x;
            double z2 = x + y, w2 = z2;
            z2 = mm_hi_part(z2);
            double v2 = y - (z2 - x);
            double a2 = -one / w2;
            double t2 = mm_hi_part(a2);
            double s2 = one + t2 * z2;
            return t2 + a2 * (s2 + t2 * v2);
        }
    }

    if (ix >= 0x3FE59428u) {
        if (hx < 0) { x = -x; y = -y; }
        double z3 = pio4 - x;
        double w3 = pio4lo - y;
        x = z3 + w3; y = 0.0;
    }

    double z = x * x;
    double w = z * z;
    double r = T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * (T[9] + w * T[11]))));
    double v = z * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * (T[10] + w * T[12])))));
    double s = z * x;
    r = y + z * (s * (r + v) + y);
    r += T[0] * s;
    w = x + r;

    if (ix >= 0x3FE59428u) {
        double vv = (double)iy;
        return (double)(1 - ((hx >> 30) & 2)) *
               (vv - 2.0 * (x - (w * w / (w + vv) - r)));
    }
    if (iy == 1) return w;

    { double zz = mm_hi_part(w);
      double vv = r - (zz - x);
      double a = -1.0 / w;
      double t = mm_hi_part(a);
      double ss = 1.0 + t * zz;
      return t + a * (ss + t * vv);
    }
}

/* ======================================================================== */
/*  sin / cos / tan / sincos                                                */
/* ======================================================================== */

static inline double mm_sin(double x) {
    mm_bits b; b.d = x;
    uint32_t ix = b.s.hi & 0x7fffffffu;
    if (ix <= 0x3fe921fbu) return mm_kernel_sin(x, 0.0, 0);
    if (ix >= 0x7ff00000u) return x - x;
    double y0, y1;
    int n = mm_rem_pio2(x, &y0, &y1);
    switch (n & 3) {
        case 0: return  mm_kernel_sin(y0, y1, 1);
        case 1: return  mm_kernel_cos(y0, y1);
        case 2: return -mm_kernel_sin(y0, y1, 1);
        default: return -mm_kernel_cos(y0, y1);
    }
}

static inline double mm_cos(double x) {
    mm_bits b; b.d = x;
    uint32_t ix = b.s.hi & 0x7fffffffu;
    if (ix <= 0x3fe921fbu) return mm_kernel_cos(x, 0.0);
    if (ix >= 0x7ff00000u) return x - x;
    double y0, y1;
    int n = mm_rem_pio2(x, &y0, &y1);
    switch (n & 3) {
        case 0: return  mm_kernel_cos(y0, y1);
        case 1: return -mm_kernel_sin(y0, y1, 1);
        case 2: return -mm_kernel_cos(y0, y1);
        default: return  mm_kernel_sin(y0, y1, 1);
    }
}

static inline double mm_tan(double x) {
    mm_bits b; b.d = x;
    uint32_t ix = b.s.hi & 0x7fffffffu;
    if (ix <= 0x3fe921fbu) return mm_kernel_tan(x, 0.0, 1);
    if (ix >= 0x7ff00000u) return x - x;
    double y0, y1;
    int n = mm_rem_pio2(x, &y0, &y1);
    return mm_kernel_tan(y0, y1, 1 - ((n & 1) << 1));
}

static inline void mm_sincos(double x, double *s, double *c) {
    mm_bits b; b.d = x;
    uint32_t ix = b.s.hi & 0x7fffffffu;
    if (ix <= 0x3fe921fbu) { *s = mm_kernel_sin(x, 0.0, 0); *c = mm_kernel_cos(x, 0.0); return; }
    if (ix >= 0x7ff00000u) { *s = x - x; *c = x - x; return; }
    double y0, y1;
    int n = mm_rem_pio2(x, &y0, &y1);
    double sv = mm_kernel_sin(y0, y1, 1);
    double cv = mm_kernel_cos(y0, y1);
    switch (n & 3) {
        case 0: *s =  sv; *c =  cv; break;
        case 1: *s =  cv; *c = -sv; break;
        case 2: *s = -sv; *c = -cv; break;
        default: *s = -cv; *c =  sv; break;
    }
}

/* ======================================================================== */
/*  asin / acos / atan / atan2                                              */
/* ======================================================================== */

static inline double mm_asin(double x) {
    static const double
        one = 1.0, huge = 1.0e300,
        pio2_hi = 1.57079632679489655800e+00,
        pio2_lo = 6.12323399573676603587e-17,
        pio4_hi = 7.85398163397448278999e-01,
        pS0 =  1.66666666666666657415e-01,
        pS1 = -3.25565818622400915405e-01,
        pS2 =  2.01212532134862925881e-01,
        pS3 = -4.00555345006794114027e-02,
        pS4 =  7.91534994289814532176e-04,
        pS5 =  3.47933107596021167570e-05,
        qS1 = -2.40339491173441421878e+00,
        qS2 =  2.02094576023350569471e+00,
        qS3 = -6.88283971605453293030e-01,
        qS4 =  7.70381505559019352791e-02;

    mm_bits xb; xb.d = x;
    int hx = (int)xb.s.hi;
    uint32_t ix = (uint32_t)hx & 0x7fffffffu;

    if (ix >= 0x3ff00000u) {
        if (((ix - 0x3ff00000u) | xb.s.lo) == 0)
            return x * pio2_hi + x * pio2_lo;
        return (x - x) / (x - x);
    }
    if (ix < 0x3fe00000u) {
        if (ix < 0x3e400000u) { if (huge + x > one) return x; }
        double t = x * x;
        double p = t * (pS0 + t * (pS1 + t * (pS2 + t * (pS3 + t * (pS4 + t * pS5)))));
        double q = one + t * (qS1 + t * (qS2 + t * (qS3 + t * qS4)));
        return x + x * (p / q);
    }
    {
        double w = one - mm_fabs(x);
        double t = w * 0.5;
        double p = t * (pS0 + t * (pS1 + t * (pS2 + t * (pS3 + t * (pS4 + t * pS5)))));
        double q = one + t * (qS1 + t * (qS2 + t * (qS3 + t * qS4)));
        double s = mm_sqrt(t);
        double result;
        if (ix >= 0x3FEF3333u) {
            double ww = p / q;
            result = pio2_hi - (2.0 * (s + s * ww) - pio2_lo);
        } else {
            double f = mm_hi_part(s);
            double c = (t - f * f) / (s + f);
            double r = p / q;
            double pp = 2.0 * s * r - (pio2_lo - 2.0 * c);
            double qq = pio4_hi - 2.0 * f;
            result = pio4_hi - (pp - qq);
        }
        return (hx > 0) ? result : -result;
    }
}

static inline double mm_acos(double x) {
    static const double
        one = 1.0,
        pi  = 3.14159265358979311600e+00,
        pio2_hi = 1.57079632679489655800e+00,
        pio2_lo = 6.12323399573676603587e-17,
        pS0 =  1.66666666666666657415e-01,
        pS1 = -3.25565818622400915405e-01,
        pS2 =  2.01212532134862925881e-01,
        pS3 = -4.00555345006794114027e-02,
        pS4 =  7.91534994289814532176e-04,
        pS5 =  3.47933107596021167570e-05,
        qS1 = -2.40339491173441421878e+00,
        qS2 =  2.02094576023350569471e+00,
        qS3 = -6.88283971605453293030e-01,
        qS4 =  7.70381505559019352791e-02;

    mm_bits xb; xb.d = x;
    int hx = (int)xb.s.hi;
    uint32_t ix = (uint32_t)hx & 0x7fffffffu;

    if (ix >= 0x3ff00000u) {
        if (((ix - 0x3ff00000u) | xb.s.lo) == 0) {
            return (hx > 0) ? 0.0 : pi + 2.0 * pio2_lo;
        }
        return (x - x) / (x - x);
    }
    if (ix < 0x3fe00000u) {
        if (ix <= 0x3c600000u) return pio2_hi + pio2_lo;
        double z = x * x;
        double p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
        double q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
        double r = p / q;
        return pio2_hi - (x - (pio2_lo - x * r));
    }
    if (hx < 0) {
        double z = (one + x) * 0.5;
        double p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
        double q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
        double s = mm_sqrt(z);
        double r = p / q;
        double w = r * s - pio2_lo;
        return pi - 2.0 * (s + w);
    }
    {
        double z = (one - x) * 0.5;
        double s = mm_sqrt(z);
        double df = mm_hi_part(s);
        double c = (z - df * df) / (s + df);
        double p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
        double q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
        double r = p / q;
        double w = r * s + c;
        return 2.0 * (df + w);
    }
}

static inline double mm_atan(double x) {
    static const double
        atanhi[] = {
            4.63647609000806093515e-01,
            7.85398163397448278999e-01,
            9.82793723247329054082e-01,
            1.57079632679489655800e+00,
        },
        atanlo[] = {
            2.26987774529616870924e-17,
            3.06161699786838301793e-17,
            1.39033110312309984516e-17,
            6.12323399573676603587e-17,
        },
        aT[] = {
            3.33333333333329318027e-01,
           -1.99999999998764832476e-01,
            1.42857142725034663711e-01,
           -1.11111104054623557880e-01,
            9.09088713343650656196e-02,
           -7.69187620504482999495e-02,
            6.66107313738753120669e-02,
           -5.83357013379057348645e-02,
            4.97687799461593236017e-02,
           -3.65315727442169155270e-02,
            1.62858201153657823623e-02,
        },
        one = 1.0, huge = 1.0e300;

    mm_bits xb; xb.d = x;
    int hx = (int)xb.s.hi;
    uint32_t ix = (uint32_t)hx & 0x7fffffffu;

    if (ix >= 0x44100000u) {
        if (ix > 0x7ff00000u || (ix == 0x7ff00000u && xb.s.lo != 0))
            return x + x;
        return (hx > 0) ? atanhi[3] + atanlo[3] : -atanhi[3] - atanlo[3];
    }

    int id;
    if (ix < 0x3fdc0000u) {
        if (ix < 0x3e200000u) { if (huge + x > one) return x; }
        id = -1;
    } else {
        x = mm_fabs(x);
        if (ix < 0x3ff30000u) {
            if (ix < 0x3fe60000u) { id = 0; x = (2.0 * x - one) / (2.0 + x); }
            else                  { id = 1; x = (x - one) / (x + one); }
        } else {
            if (ix < 0x40038000u) { id = 2; x = (x - 1.5) / (one + 1.5 * x); }
            else                  { id = 3; x = -1.0 / x; }
        }
    }

    double z = x * x;
    double w = z * z;
    double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));

    if (id < 0) return x - x * (s1 + s2);
    z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
    return (hx < 0) ? -z : z;
}

static inline double mm_atan2(double y, double x) {
    static const double
        tiny   = 1.0e-300,
        zero   = 0.0,
        pi_o_4 = 7.8539816339744827900E-01,
        pi_o_2 = 1.5707963267948965580E+00,
        pi     = 3.1415926535897931160E+00,
        pi_lo  = 1.2246467991473531772E-16;

    mm_bits xb, yb; xb.d = x; yb.d = y;
    int hx = (int)xb.s.hi, hy = (int)yb.s.hi;
    uint32_t ix = (uint32_t)hx & 0x7fffffffu, iy = (uint32_t)hy & 0x7fffffffu;
    uint32_t lx = xb.s.lo, ly = yb.s.lo;

    if (((ix | ((lx | (uint32_t)(-(int32_t)lx)) >> 31)) > 0x7ff00000u) ||
        ((iy | ((ly | (uint32_t)(-(int32_t)ly)) >> 31)) > 0x7ff00000u))
        return x + y;

    if (((hx - 0x3ff00000) | (int)lx) == 0) return mm_atan(y);

    int m = ((hy >> 31) & 1) | ((hx >> 30) & 2);

    if ((iy | ly) == 0) {
        switch (m) {
            case 0: case 1: return y;
            case 2: return  pi + tiny;
            case 3: return -pi - tiny;
        }
    }
    if ((ix | lx) == 0) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;

    if (ix == 0x7ff00000u) {
        if (iy == 0x7ff00000u) {
            switch (m) {
                case 0: return  pi_o_4 + tiny;
                case 1: return -pi_o_4 - tiny;
                case 2: return  3.0 * pi_o_4 + tiny;
                case 3: return -3.0 * pi_o_4 - tiny;
            }
        } else {
            switch (m) {
                case 0: return  zero;
                case 1: return -zero;
                case 2: return  pi + tiny;
                case 3: return -pi - tiny;
            }
        }
    }
    if (iy == 0x7ff00000u) return (hy < 0) ? -pi_o_2 - tiny : pi_o_2 + tiny;

    {
        int k = (int)(iy - ix) >> 20;
        double z;
        if (k > 60) z = pi_o_2 + 0.5 * pi_lo;
        else if (hx < 0 && k < -60) z = 0.0;
        else z = mm_atan(mm_fabs(y / x));
        switch (m) {
            case 0: return  z;
            case 1: { mm_bits zb; zb.d = z; zb.s.hi ^= 0x80000000u; return zb.d; }
            case 2: return  pi - (z - pi_lo);
            default: return (z - pi_lo) - pi;
        }
    }
}

/* ======================================================================== */
/*  sinh / cosh / tanh                                                      */
/* ======================================================================== */

/* 内部 expm1 */
static inline double mm_expm1(double x) {
    static const double
        one = 1.0, huge = 1.0e+300, tiny = 1.0e-300,
        o_threshold = 7.09782712893383973096e+02,
        ln2_hi = 6.93147180369123816490e-01,
        ln2_lo = 1.90821492927058770002e-10,
        invln2 = 1.44269504088896338700e+00,
        Q1 = -3.33333333333331316428e-02,
        Q2 =  1.58730158725481460165e-03,
        Q3 = -7.93650757867487942473e-05,
        Q4 =  4.00821782732936239552e-06,
        Q5 = -2.01099218183624371326e-07;

    mm_bits xb; xb.d = x;
    int xsb = (int)(xb.s.hi >> 31);
    uint32_t hx = xb.s.hi & 0x7fffffffu;

    if (hx >= 0x7ff00000u) {
        if (((hx & 0xfffffu) | xb.s.lo) != 0) return x + x;
        return xsb ? -1.0 : x;
    }
    if (x > o_threshold) return huge * huge;
    if (xsb && x + tiny < 0.0) return tiny - one;

    double hi, lo, c;
    int k;
    if (hx > 0x3fd62e42u) {
        if (hx < 0x3ff0a2b2u) {
            if (!xsb) { hi = x - ln2_hi; lo = ln2_lo; k = 1; }
            else      { hi = x + ln2_hi; lo = -ln2_lo; k = -1; }
        } else {
            k = (int)(invln2 * x + (xsb ? -0.5 : 0.5));
            double t = (double)k;
            hi = x - t * ln2_hi; lo = t * ln2_lo;
        }
        x = hi - lo; c = (hi - x) - lo;
    } else if (hx < 0x3c900000u) {
        double t = huge + x; return x - (t - (huge + x));
    } else { k = 0; c = 0; hi = x; lo = 0; }

    double hfx = 0.5 * x;
    double hxs = x * hfx;
    double r1 = one + hxs * (Q1 + hxs * (Q2 + hxs * (Q3 + hxs * (Q4 + hxs * Q5))));
    double t = 3.0 - r1 * hfx;
    double e = hxs * ((r1 - t) / (6.0 - x * t));

    if (k == 0) return x - (x * e - hxs);
    e = (x * (e - c) - c) - hxs;
    if (k == -1) return 0.5 * (x - e) - 0.5;
    if (k == 1) {
        if (x < -0.25) return -2.0 * (e - (x + 0.5));
        return one + 2.0 * (x - e);
    }
    if (k <= -2 || k > 56) {
        double yy = one - (e - x);
        mm_bits yb; yb.d = yy;
        yb.s.hi += ((uint32_t)k << 20);
        return yb.d - one;
    }
    if (k < 20) {
        mm_bits tb; tb.d = one;
        tb.s.hi = 0x3ff00000u - (0x200000u >> k);
        double yy = tb.d - (e - x);
        mm_bits yb; yb.d = yy;
        yb.s.hi += ((uint32_t)k << 20);
        return yb.d;
    } else {
        mm_bits tb; tb.d = one;
        tb.s.hi = ((uint32_t)(0x3ff - k) << 20);
        double yy = x - (e + tb.d) + one;
        mm_bits yb; yb.d = yy;
        yb.s.hi += ((uint32_t)k << 20);
        return yb.d;
    }
}

static inline double mm_sinh(double x) {
    static const double one = 1.0, shuge = 1.0e307;
    double h = (x < 0) ? -0.5 : 0.5;
    mm_bits xb; xb.d = x;
    uint32_t ix = xb.s.hi & 0x7fffffffu;

    if (ix >= 0x7ff00000u) return x + x;
    if (ix < 0x40360000u) {
        if (ix < 0x3e300000u) { if (shuge + x > one) return x; }
        double t = mm_expm1(mm_fabs(x));
        if (ix < 0x3ff00000u) return h * (2.0 * t - t * t / (t + one));
        return h * (t + t / (t + one));
    }
    if (ix < 0x40862E42u) return h * mm_exp(mm_fabs(x));
    if (ix <= 0x408633CEu) {
        double w = mm_exp(0.5 * mm_fabs(x));
        double t = h * w;
        return t * w;
    }
    return x * shuge;
}

static inline double mm_cosh(double x) {
    static const double one = 1.0, half = 0.5, huge = 1.0e300;
    mm_bits xb; xb.d = x;
    uint32_t ix = xb.s.hi & 0x7fffffffu;

    if (ix >= 0x7ff00000u) return x * x;
    if (ix < 0x3fd62e43u) {
        double t = mm_expm1(mm_fabs(x));
        double w = one + t;
        if (ix < 0x3c800000u) return w;
        return one + (t * t) / (w + w);
    }
    if (ix < 0x40360000u) {
        double t = mm_exp(mm_fabs(x));
        return half * t + half / t;
    }
    if (ix < 0x40862E42u) return half * mm_exp(mm_fabs(x));
    if (ix <= 0x408633CEu) {
        double w = mm_exp(half * mm_fabs(x));
        double t = half * w;
        return t * w;
    }
    return huge * huge;
}

static inline double mm_tanh(double x) {
    static const double one = 1.0, two = 2.0, tiny = 1.0e-300;
    mm_bits xb; xb.d = x;
    int jx = (int)xb.s.hi;
    uint32_t ix = (uint32_t)jx & 0x7fffffffu;

    if (ix >= 0x7ff00000u) {
        if (jx >= 0) return one / x + one;
        return one / x - one;
    }
    double z;
    if (ix < 0x40360000u) {
        if (ix < 0x3c800000u) return x * (one + x);
        if (ix >= 0x3ff00000u) {
            double t = mm_expm1(two * mm_fabs(x));
            z = one - two / (t + two);
        } else {
            double t = mm_expm1(-two * mm_fabs(x));
            z = -t / (t + two);
        }
    } else {
        z = one - tiny;
    }
    return (jx >= 0) ? z : -z;
}

/* ======================================================================== */
/*  exp / log / log10                                                       */
/* ======================================================================== */

static inline double mm_exp(double x) {
    static const double
        one = 1.0, halF = 0.5,
        o_threshold =  7.09782712893383973096e+02,
        u_threshold = -7.45133219101941108420e+02,
        ln2HI =  6.93147180369123816490e-01,
        ln2LO =  1.90821492927058770002e-10,
        invln2 = 1.44269504088896338700e+00,
        P1 =  1.66666666666666019037e-01,
        P2 = -2.77777777770155933842e-03,
        P3 =  6.61375632143793436117e-05,
        P4 = -1.65339022054652515390e-06,
        P5 =  4.13813679705723846039e-08;

    mm_bits xb; xb.d = x;
    uint32_t hx = xb.s.hi & 0x7fffffffu;
    int xsb = (int)(xb.s.hi >> 31);

    if (hx >= 0x7ff00000u) {
        if (((hx & 0xfffffu) | xb.s.lo) != 0) return x;
        return xsb ? 0.0 : x;
    }
    if (x > o_threshold) return 1e308 * 1e308;
    if (x < u_threshold) return 0.0;

    double hi, lo;
    int k;
    if (hx > 0x3fd62e42u) {
        if (hx < 0x3ff0a2b2u) {
            if (!xsb) { hi = x - ln2HI; lo = ln2LO; k = 1; }
            else      { hi = x + ln2HI; lo = -ln2LO; k = -1; }
        } else {
            k = (int)(invln2 * x + (xsb ? -halF : halF));
            double t = (double)k;
            hi = x - t * ln2HI; lo = t * ln2LO;
        }
        x = hi - lo;
    } else if (hx < 0x3e300000u) {
        return one + x;
    } else { k = 0; hi = x; lo = 0; }

    double t = x * x;
    double c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    double y = one - ((lo - (x * c) / (2.0 - c)) - hi);
    if (k == 0) return y;
    return mm_scalbn(y, k);
}

static inline double mm_log(double x) {
    static const double
        ln2_hi = 6.93147180369123816490e-01,
        ln2_lo = 1.90821492927058770002e-10,
        two54  = 1.80143985094819840000e+16,
        Lg1 = 6.666666666666735130e-01,
        Lg2 = 3.999999999940941908e-01,
        Lg3 = 2.857142874366239149e-01,
        Lg4 = 2.222219843214978396e-01,
        Lg5 = 1.818357216161805012e-01,
        Lg6 = 1.531383769920937332e-01,
        Lg7 = 1.479819860511658591e-01;

    mm_bits xb; xb.d = x;
    uint32_t hx = xb.s.hi;
    int k = 0;

    if (hx < 0x00100000u) {
        if (((hx & 0x7fffffffu) | xb.s.lo) == 0) return -1e308 / 0.0;
        if (hx & 0x80000000u) return (x - x) / 0.0;
        k -= 54; x *= two54; xb.d = x; hx = xb.s.hi;
    }
    if (hx >= 0x7ff00000u) return x + x;

    k += (int)(hx >> 20) - 1023;
    int i = (int)(((uint32_t)k & 0x80000000u) >> 31);
    hx = (hx & 0x000fffffu) | ((uint32_t)(0x3ff - i) << 20);
    xb.s.hi = hx; x = xb.d;

    double f = x - 1.0;
    double s = f / (2.0 + f);
    double z = s * s;
    double w = z * z;
    double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    double R = t2 + t1;
    double hfsq = 0.5 * f * f;

    if (k == 0) return f - (hfsq - s * (hfsq + R));
    return (double)k * ln2_hi - ((hfsq - (s * (hfsq + R) + (double)k * ln2_lo)) - f);
}


static inline double mm_log2(double x) {
    return mm_log(x) * 1.44269504088896340736; 
}

static inline double mm_log10(double x) {
    static const double
        two54     = 1.80143985094819840000e+16,
        ivln10    = 4.34294481903251816668e-01,
        log10_2hi = 3.01029995663611771306e-01,
        log10_2lo = 3.69423907715893078616e-13;

    mm_bits xb; xb.d = x;
    uint32_t hx = xb.s.hi;
    int k = 0;

    if (hx < 0x00100000u) {
        if (((hx & 0x7fffffffu) | xb.s.lo) == 0) return -1e308 / 0.0;
        if (hx & 0x80000000u) return (x - x) / 0.0;
        k -= 54; x *= two54; xb.d = x; hx = xb.s.hi;
    }
    if (hx >= 0x7ff00000u) return x + x;

    k += (int)(hx >> 20) - 1023;
    int i = (int)(((uint32_t)k & 0x80000000u) >> 31);
    hx = (hx & 0x000fffffu) | ((uint32_t)(0x3ff - i) << 20);
    double y = (double)(k + i);
    xb.s.hi = hx; x = xb.d;

    return y * log10_2lo + ivln10 * mm_log(x) + y * log10_2hi;
}



static inline double mm_sqrt(double x) {
 // ---------- sqrt（硬件指令，RISC-V / LoongArch） ----------

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

/* ======================================================================== */
/*  ceil / floor                                                            */
/* ======================================================================== */

static inline double mm_ceil(double x) {
    static const double huge = 1.0e300;
    mm_bits b; b.d = x;
    int i0 = (int)b.s.hi, j0;
    uint32_t i1 = b.s.lo, i;

    j0 = (int)(((uint32_t)i0 >> 20) & 0x7ff) - 0x3ff;
    if (j0 < 20) {
        if (j0 < 0) {
            if (huge + x > 0.0) {
                if (i0 < 0) { i0 = (int)0x80000000; i1 = 0; }
                else if ((i0 | (int)i1) != 0) { i0 = 0x3ff00000; i1 = 0; }
            }
        } else {
            i = 0x000fffffu >> j0;
            if (((uint32_t)i0 & i | i1) == 0) return x;
            if (huge + x > 0.0) {
                if (i0 > 0) i0 += (int)(0x00100000u >> j0);
                i0 &= ~(int)i; i1 = 0;
            }
        }
    } else if (j0 > 51) {
        if (j0 == 0x400) return x + x;
        return x;
    } else {
        i = 0xffffffffu >> (j0 - 20);
        if ((i1 & i) == 0) return x;
        if (huge + x > 0.0) {
            if (i0 > 0) {
                if (j0 == 20) i0 += 1;
                else { uint32_t j = i1 + (1u << (52 - j0)); if (j < i1) i0 += 1; i1 = j; }
            }
            i1 &= ~i;
        }
    }
    b.s.hi = (uint32_t)i0; b.s.lo = i1;
    return b.d;
}

static inline double mm_floor(double x) {
    static const double huge = 1.0e300;
    mm_bits b; b.d = x;
    int i0 = (int)b.s.hi, j0;
    uint32_t i1 = b.s.lo, i;

    j0 = (int)(((uint32_t)i0 >> 20) & 0x7ff) - 0x3ff;
    if (j0 < 20) {
        if (j0 < 0) {
            if (huge + x > 0.0) {
                if (i0 >= 0) { i0 = 0; i1 = 0; }
                else if (((i0 & 0x7fffffff) | (int)i1) != 0) { i0 = (int)0xbff00000; i1 = 0; }
            }
        } else {
            i = 0x000fffffu >> j0;
            if (((uint32_t)i0 & i | i1) == 0) return x;
            if (huge + x > 0.0) {
                if (i0 < 0) i0 += (int)(0x00100000u >> j0);
                i0 &= ~(int)i; i1 = 0;
            }
        }
    } else if (j0 > 51) {
        if (j0 == 0x400) return x + x;
        return x;
    } else {
        i = 0xffffffffu >> (j0 - 20);
        if ((i1 & i) == 0) return x;
        if (huge + x > 0.0) {
            if (i0 < 0) {
                if (j0 == 20) i0 += 1;
                else { uint32_t j = i1 + (1u << (52 - j0)); if (j < i1) i0 += 1; i1 = j; }
            }
            i1 &= ~i;
        }
    }
    b.s.hi = (uint32_t)i0; b.s.lo = i1;
    return b.d;
}

/* ======================================================================== */
/*  fmod / modf                                                             */
/* ======================================================================== */

static inline double mm_fmod(double x, double y) {
    mm_bits xb, yb; xb.d = x; yb.d = y;
    int hx = (int)xb.s.hi, hy = (int)yb.s.hi;
    uint32_t lx = xb.s.lo, ly = yb.s.lo;
    int sx = hx & (int)0x80000000;
    hx ^= sx; hy &= 0x7fffffff;

    if ((hy | (int)ly) == 0 || hx >= 0x7ff00000 ||
        ((hy | (int)((ly | (uint32_t)(-(int32_t)ly)) >> 31)) > 0x7ff00000))
        return (x * y) / (x * y);

    if ((uint32_t)hx <= (uint32_t)hy) {
        if ((uint32_t)hx < (uint32_t)hy || lx < ly) return x;
        if (lx == ly) { mm_bits z; z.u = 0; z.s.hi = (uint32_t)sx; return z.d; }
    }

    int ix, iy, ii;
    if (hx < 0x00100000) {
        if (hx == 0) { for (ix = -1043, ii = (int)lx; ii > 0; ii <<= 1) ix--; }
        else         { for (ix = -1022, ii = hx << 11; ii > 0; ii <<= 1) ix--; }
    } else ix = (hx >> 20) - 1023;

    if (hy < 0x00100000) {
        if (hy == 0) { for (iy = -1043, ii = (int)ly; ii > 0; ii <<= 1) iy--; }
        else         { for (iy = -1022, ii = hy << 11; ii > 0; ii <<= 1) iy--; }
    } else iy = (hy >> 20) - 1023;

    if (ix >= -1022) hx = 0x00100000 | (0x000fffff & hx);
    else { int n = -1022 - ix;
        if (n <= 31) { hx = (hx << n) | (int)(lx >> (32 - n)); lx <<= n; }
        else { hx = (int)(lx << (n - 32)); lx = 0; } }
    if (iy >= -1022) hy = 0x00100000 | (0x000fffff & hy);
    else { int n = -1022 - iy;
        if (n <= 31) { hy = (hy << n) | (int)(ly >> (32 - n)); ly <<= n; }
        else { hy = (int)(ly << (n - 32)); ly = 0; } }

    int n = ix - iy;
    while (n--) {
        int hz = hx - hy; uint32_t lz = lx - ly;
        if (lx < ly) hz--;
        if (hz < 0) { hx = hx + hx + (int)(lx >> 31); lx += lx; }
        else {
            if ((hz | (int)lz) == 0) { mm_bits z; z.u = 0; z.s.hi = (uint32_t)sx; return z.d; }
            hx = hz + hz + (int)(lz >> 31); lx = lz + lz;
        }
    }
    { int hz = hx - hy; uint32_t lz = lx - ly;
      if (lx < ly) hz--;
      if (hz >= 0) { hx = hz; lx = lz; } }

    if ((hx | (int)lx) == 0) { mm_bits z; z.u = 0; z.s.hi = (uint32_t)sx; return z.d; }
    while (hx < 0x00100000) { hx = hx + hx + (int)(lx >> 31); lx += lx; iy--; }

    mm_bits rb;
    if (iy >= -1022) {
        hx = ((hx - 0x00100000) | ((iy + 1023) << 20));
        rb.s.hi = (uint32_t)hx | (uint32_t)sx; rb.s.lo = lx;
    } else {
        int nn = -1022 - iy;
        if (nn <= 20) { lx = (lx >> nn) | ((uint32_t)hx << (32 - nn)); hx >>= nn; }
        else if (nn <= 31) { lx = ((uint32_t)hx << (32 - nn)) | (lx >> nn); hx = sx; }
        else { lx = (uint32_t)hx >> (nn - 32); hx = sx; }
        rb.s.hi = (uint32_t)hx | (uint32_t)sx; rb.s.lo = lx;
    }
    return rb.d;
}

static inline double mm_modf(double x, double *iptr) {
    mm_bits b; b.d = x;
    int i0 = (int)b.s.hi, j0;
    uint32_t i1 = b.s.lo, i;

    j0 = (int)(((uint32_t)i0 >> 20) & 0x7ff) - 0x3ff;
    if (j0 < 20) {
        if (j0 < 0) {
            mm_bits ib; ib.u = 0; ib.s.hi = (uint32_t)i0 & 0x80000000u;
            *iptr = ib.d; return x;
        }
        i = 0x000fffffu >> j0;
        if (((uint32_t)i0 & i | i1) == 0) {
            *iptr = x;
            mm_bits zb; zb.u = 0; zb.s.hi = (uint32_t)i0 & 0x80000000u;
            return zb.d;
        }
        mm_bits ib; ib.d = x;
        ib.s.hi = (uint32_t)i0 & ~i; ib.s.lo = 0;
        *iptr = ib.d;
        return x - *iptr;
    }
    if (j0 > 51) {
        *iptr = x;
        mm_bits zb; zb.u = 0; zb.s.hi = (uint32_t)i0 & 0x80000000u;
        return zb.d;
    }
    i = 0xffffffffu >> (j0 - 20);
    if ((i1 & i) == 0) {
        *iptr = x;
        mm_bits zb; zb.u = 0; zb.s.hi = (uint32_t)i0 & 0x80000000u;
        return zb.d;
    }
    mm_bits ib; ib.d = x; ib.s.lo = i1 & ~i;
    *iptr = ib.d;
    return x - *iptr;
}

/* ---- pow（简化版，用 exp + log 实现） ---- */
static inline double mm_pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return 0.0;
    if (x == 1.0) return 1.0;
    /* 负底数 + 非整数指数 → NaN */
    if (x < 0.0) {
        double yi = (double)(int64_t)y;
        if (yi != y) return (x - x) / (x - x); /* NaN */
        double r = mm_exp(y * mm_log(-x));
        int64_t n = (int64_t)y;
        return (n & 1) ? -r : r;
    }
    return mm_exp(y * mm_log(x));
}

#ifndef HUGE_VAL
#define HUGE_VAL    (1.0 / 0.0)
#endif
#ifndef HUGE_VALF
#define HUGE_VALF   (1.0f / 0.0f)
#endif

#ifndef __cplusplus

/* double 版本 */
#define sin(x)        mm_sin(x)
#define cos(x)        mm_cos(x)
#define tan(x)        mm_tan(x)
#define asin(x)       mm_asin(x)
#define acos(x)       mm_acos(x)
#define atan(x)       mm_atan(x)
#define atan2(y,x)    mm_atan2(y,x)
#define sinh(x)       mm_sinh(x)
#define cosh(x)       mm_cosh(x)
#define tanh(x)       mm_tanh(x)
#define exp(x)        mm_exp(x)
#define log(x)        mm_log(x)
#define log2(x)       mm_log2(x)
#define log10(x)      mm_log10(x)
#define sqrt(x)       mm_sqrt(x)
#define ceil(x)       mm_ceil(x)
#define floor(x)      mm_floor(x)
#define fmod(x,y)     mm_fmod(x,y)
#define fabs(x)       mm_fabs(x)
#define pow(x,y)      mm_pow(x,y)

/* 带指针的单独给 inline（宏处理多参数指针容易出错） */
static inline void   sincos(double x, double *s, double *c) { mm_sincos(x, s, c); }
static inline double modf(double x, double *ip)             { return mm_modf(x, ip); }

/* float 版本（C 标准库风格：sinf / cosf / ...） */
#define sinf(x)       ((float)mm_sin((double)(x)))
#define cosf(x)       ((float)mm_cos((double)(x)))
#define tanf(x)       ((float)mm_tan((double)(x)))
#define asinf(x)      ((float)mm_asin((double)(x)))
#define acosf(x)      ((float)mm_acos((double)(x)))
#define atanf(x)      ((float)mm_atan((double)(x)))
#define atan2f(y,x)   ((float)mm_atan2((double)(y),(double)(x)))
#define sinhf(x)      ((float)mm_sinh((double)(x)))
#define coshf(x)      ((float)mm_cosh((double)(x)))
#define tanhf(x)      ((float)mm_tanh((double)(x)))
#define expf(x)       ((float)mm_exp((double)(x)))
#define logf(x)       ((float)mm_log((double)(x)))
#define log10f(x)     ((float)mm_log10((double)(x)))
#define sqrtf(x)      ((float)mm_sqrt((double)(x)))
#define ceilf(x)      ((float)mm_ceil((double)(x)))
#define floorf(x)     ((float)mm_floor((double)(x)))
#define fmodf(x,y)    ((float)mm_fmod((double)(x),(double)(y)))
#define fabsf(x)      ((float)mm_fabs((double)(x)))
#define powf(x,y)     ((float)mm_pow((double)(x),(double)(y)))

static inline void sincosf(float x, float *s, float *c) {
    double ds, dc; mm_sincos((double)x, &ds, &dc);
    *s = (float)ds; *c = (float)dc;
}
static inline float modff(float x, float *ip) {
    double di; float r = (float)mm_modf((double)x, &di);
    *ip = (float)di; return r;
}


static inline double ldexp(double x, int n) {
    return x * mm_pow(2.0, (double)n);
}

static inline float ldexpf(float x, int n) {
    return (float)((double)x * mm_pow(2.0, (double)n));
}

/* long double 版(riscv64 = 128 位 quad): 教学简化经 double 计算(TCC tccpp.c 需要) */
static inline long double ldexpl(long double x, int n) {
    return (long double)ldexp((double)x, n);
}

static inline double frexp(double x, int *exp) {
    if (x == 0.0) { *exp = 0; return 0.0; }
    union { double d; unsigned long long u; } b;
    b.d = x;
    int e = (int)((b.u >> 52) & 0x7FF) - 1022;
    b.u = (b.u & 0x800FFFFFFFFFFFFFULL) | 0x3FE0000000000000ULL;
    *exp = e;
    return b.d;
}

static inline float frexpf(float x, int *exp) {
    double r = frexp((double)x, exp);
    return (float)r;
}




#endif /* __cplusplus */

#define PI  3.14159265358979323846