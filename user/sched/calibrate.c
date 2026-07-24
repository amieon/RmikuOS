/*
 * calibrate.c v2 —— RmikuOS 实验环境完整标定
 *
 * 三个阶段:
 *   Phase 1  Tick ↔ Time 线性关系证明
 *            5 个 tick 跨度 × N 次重复, 线性回归出 R²,
 *            证明 Δtime = k·Δticks 严格成立。
 *
 *   Phase 2  Burn-iters 扫描
 *            6 个 iters 值 × N 次重复, 建立 iters → ticks 映射,
 *            末尾直接输出 "要 X tick 就用 burn(Y)" 推荐表。
 *
 *   Phase 3  运行时漂移检测
 *            前后各测一次 tick 速率, 偏差 > 5% 告警。
 *
 * 用法:  ./calibrate [reps]       reps 默认 5, 最大 10
 *
 * 输出 CSV (供宿主机 Python 解析):
 *   L,<span>,<rep>,<delta_us>
 *   LS,<span>,<mean_us>,<std_us>,<us_per_tick_x1000>
 *   LR,<slope_x1000>,<intercept_us>,<r2_x10000>
 *   B,<iters>,<rep>,<total_ticks>,<inner>
 *   BS,<iters>,<mean_ticks_x1000>,<std_ticks_x1000>
 *   DRIFT,<pre_x1000>,<post_x1000>,<pct>
 */
#include "user.h"
#include "schedlab.h"          /* sl_burn() */

/* ---------- 平台标识 ---------- */
#ifdef __riscv
#define PLATFORM "riscv64"
#elif defined(__loongarch__)
#define PLATFORM "loongarch64"
#else
#define PLATFORM "unknown"
#endif

/* ---------- 配置 ---------- */
#define N_SPANS       5
#define N_BURNS       6
#define MAX_REPS     10
#define DRIFT_THRESH  5       /* % */
#define TARGET_TICKS  200     /* burn 测量目标跨度 */

static const unsigned long span_list[N_SPANS] =
    { 200, 500, 1000, 2000, 4000 };
static const unsigned long burn_list[N_BURNS] =
    { 50000, 100000, 200000, 400000, 800000, 1600000 };

/* ---------- 整数工具 ---------- */
static unsigned long isqrt_ul(unsigned long v)
{
    unsigned long r = 0, bit = 1UL << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else               r >>= 1;
        bit >>= 2;
    }
    return r;
}

typedef struct { unsigned long mean, std, vmin, vmax; } stat_t;

static stat_t stats(unsigned long *v, int n)
{
    stat_t s;
    unsigned long sum = 0, sumsq = 0;
    s.vmin = v[0]; s.vmax = v[0];
    for (int i = 0; i < n; i++) {
        sum   += v[i];
        sumsq += v[i] * v[i];          /* u64: max ~10^9² = 10^18, safe */
        if (v[i] < s.vmin) s.vmin = v[i];
        if (v[i] > s.vmax) s.vmax = v[i];
    }
    s.mean = sum / (unsigned long)n;
    unsigned long var = sumsq / (unsigned long)n - s.mean * s.mean;
    s.std = isqrt_ul(var);
    return s;
}

/* ================================================================
 *  Phase 1 : Tick ↔ Time 线性关系
 * ================================================================ */
static unsigned long p1_mean_us[N_SPANS];   /* 留给 summary 用 */

static void phase1(int reps)
{
    printf("# === Phase 1: Tick-Time Linearity ===\n");

    for (int si = 0; si < N_SPANS; si++) {
        unsigned long span = span_list[si];
        unsigned long buf[MAX_REPS];

        for (int r = 0; r < reps; r++) {
            unsigned long t0  = get_ticks();
            unsigned long us0 = get_time();
            while (get_ticks() - t0 < span)
                ;                       /* 忙等, 纯测时钟 */
            unsigned long dt  = get_ticks() - t0;
            unsigned long dus = get_time() - us0;
            buf[r] = dus;
            printf("L,%lu,%d,%lu\n", span, r, dus);
        }

        stat_t s   = stats(buf, reps);
        p1_mean_us[si] = s.mean;
        unsigned long upt = s.mean * 1000 / span;   /* μs/tick ×1000 */
        printf("LS,%lu,%lu,%lu,%lu\n", span, s.mean, s.std, upt);
        printf("#   span=%4lu  mean=%lu us  std=%lu us  rate=%lu.%03lu us/tick\n",
               span, s.mean, s.std, upt / 1000, upt % 1000);
    }

    /* ---- 最小二乘线性回归  y = slope·x + intercept ---- */
    unsigned long long sx = 0, sy = 0, sxy = 0, sxx = 0;
    for (int i = 0; i < N_SPANS; i++) {
        unsigned long long x = span_list[i];
        unsigned long long y = p1_mean_us[i];
        sx  += x;
        sy  += y;
        sxy += x * y;
        sxx += x * x;
    }
    unsigned long long n   = N_SPANS;
    unsigned long long num = n * sxy - sx * sy;     /* 斜率分子 */
    unsigned long long den = n * sxx - sx * sx;     /* 斜率分母 */

    unsigned long slope_x1000 = (unsigned long)(num * 1000 / den);
    long long intercept = (long long)(sy * den - num * sx)
                        / (long long)(n * den);

    /* R² = 1 − SS_res / SS_tot */
    unsigned long long ss_tot = 0, ss_res = 0;
    unsigned long long y_bar  = sy / n;
    for (int i = 0; i < N_SPANS; i++) {
        long long dy   = (long long)p1_mean_us[i] - (long long)y_bar;
        ss_tot += (unsigned long long)(dy * dy);
        long long pred = (long long)(slope_x1000 * span_list[i] / 1000)
                       + intercept;
        long long res  = (long long)p1_mean_us[i] - pred;
        ss_res += (unsigned long long)(res * res);
    }
    unsigned long r2 = ss_tot
        ? (unsigned long)(10000 - ss_res * 10000 / ss_tot)
        : 10000;

    printf("LR,%lu,", (unsigned long)slope_x1000);
    if (intercept < 0) printf("-%lu,", (unsigned long)(-intercept));
    else               printf("%lu,",  (unsigned long)intercept);
    printf("%lu\n", r2);

    printf("# slope = %lu.%03lu us/tick\n",
           slope_x1000 / 1000, slope_x1000 % 1000);
    if (intercept < 0)
        printf("# intercept = -%lu us\n", (unsigned long)(-intercept));
    else
        printf("# intercept = %lu us\n",  (unsigned long)intercept);
    printf("# R2 = %lu.%04lu\n", r2 / 10000, r2 % 10000);

    if (r2 >= 9990)
        printf("# VERDICT: LINEAR (R2 >= 0.999) -- tick is a faithful clock\n");
    else
        printf("# VERDICT: NON-LINEAR (R2 < 0.999) -- investigate timer!\n");
}

/* ================================================================
 *  Phase 2 : Burn-iters 扫描
 * ================================================================ */
static unsigned long p2_mean_x1000[N_BURNS];   /* 留给 summary 用 */

static void phase2(int reps)
{
    printf("# === Phase 2: Burn-iters Sweep ===\n");

    for (int bi = 0; bi < N_BURNS; bi++) {
        unsigned long iters = burn_list[bi];

        /* 自适应 inner: 让每次测量跨 ~TARGET_TICKS 个 tick */
        unsigned long t0  = get_ticks();
        sl_burn(iters);
        unsigned long est = get_ticks() - t0;
        if (est == 0) est = 1;
        unsigned long inner = TARGET_TICKS / est;
        if (inner < 1)    inner = 1;
        if (inner > 2000) inner = 2000;

        unsigned long buf[MAX_REPS];
        for (int r = 0; r < reps; r++) {
            t0 = get_ticks();
            for (unsigned long k = 0; k < inner; k++)
                sl_burn(iters);
            unsigned long dt = get_ticks() - t0;
            buf[r] = dt * 1000 / inner;     /* ticks ×1000 */
            printf("B,%lu,%d,%lu,%lu\n", iters, r, dt, inner);
        }

        stat_t s = stats(buf, reps);
        p2_mean_x1000[bi] = s.mean;
        printf("BS,%lu,%lu,%lu\n", iters, s.mean, s.std);
        printf("#   burn(%7lu) = %lu.%03lu ticks  (std %lu.%03lu, inner=%lu)\n",
               iters,
               s.mean / 1000, s.mean % 1000,
               s.std  / 1000, s.std  % 1000,
               inner);
    }
}

/* ================================================================
 *  Phase 3 : 漂移检测
 * ================================================================ */
static void phase3(void)
{
    printf("# === Phase 3: Drift Check ===\n");

    unsigned long t0  = get_ticks();
    unsigned long us0 = get_time();
    while (get_ticks() - t0 < 2000) ;
    unsigned long pre = (get_time() - us0) * 1000 / (get_ticks() - t0);

    t0  = get_ticks();
    us0 = get_time();
    while (get_ticks() - t0 < 4000) ;
    unsigned long post = (get_time() - us0) * 1000 / (get_ticks() - t0);

    unsigned long diff = pre > post ? pre - post : post - pre;
    unsigned long pct  = diff * 100 / pre;

    printf("DRIFT,%lu,%lu,%lu\n", pre, post, pct);
    if (pct > DRIFT_THRESH)
        printf("# [WARN] drift %lu%% > %d%% -- host may be overloaded\n",
               pct, DRIFT_THRESH);
    else
        printf("# [OK] drift %lu%% <= %d%%\n", pct, DRIFT_THRESH);
}

/* ================================================================
 *  Summary : 推荐 burn 值
 * ================================================================ */
static void summary(void)
{
    printf("# === Recommendations ===\n");

    /* 用 burn(400000) 的测量值做锚点 (index 3) */
    unsigned long ref_iters = burn_list[3];          /* 400000 */
    unsigned long ref_tx    = p2_mean_x1000[3];      /* ticks×1000 */
    if (ref_tx == 0) ref_tx = 1;

    /* iters_per_tick = ref_iters / (ref_tx/1000) = ref_iters*1000/ref_tx */
    unsigned long ipt = ref_iters * 1000 / ref_tx;   /* iters per tick */

    printf("# anchor: burn(%lu) = %lu.%03lu ticks\n",
           ref_iters, ref_tx / 1000, ref_tx % 1000);
    printf("# iters_per_tick ~ %lu\n", ipt);
    printf("#\n");
    printf("# target_ticks | recommended burn(iters)\n");
    printf("# -------------|------------------------\n");
    for (int t = 1; t <= 8; t++) {
        unsigned long rec = ipt * (unsigned long)t;
        /* 取整到千位, 好看 */
        rec = (rec + 500) / 1000 * 1000;
        printf("#   %d tick(s)   |  burn(%lu)\n", t, rec);
    }

    /* 线性度检查: 最大点 vs 锚点的比值应 ≈ iters 比值 */
    unsigned long hi_iters = burn_list[N_BURNS - 1];
    unsigned long hi_tx    = p2_mean_x1000[N_BURNS - 1];
    if (hi_tx > 0 && ref_tx > 0) {
        /* 期望: hi_tx/ref_tx ≈ hi_iters/ref_iters */
        unsigned long expect = ref_tx * hi_iters / ref_iters;
        unsigned long diff   = hi_tx > expect ? hi_tx - expect
                                              : expect - hi_tx;
        unsigned long pct    = diff * 100 / expect;
        printf("#\n");
        printf("# burn linearity: burn(%lu)/burn(%lu) deviation = %lu%%\n",
               hi_iters, ref_iters, pct);
        if (pct <= 10)
            printf("# VERDICT: burn is linear in iters (dev <= 10%%)\n");
        else
            printf("# VERDICT: burn NON-linear (dev %lu%%) -- use lookup table\n",
                   pct);
    }
}

/* ================================================================ */
int main(int argc, char **argv)
{
    int reps = 5;
    if (argc > 1) {
        reps = parse_int(argv[1]);
        if (reps < 1)        reps = 1;
        if (reps > MAX_REPS) reps = MAX_REPS;
    }

    printf("# calibrate v2  platform=%s  reps=%d\n", PLATFORM, reps);
    printf("# L,<span>,<rep>,<delta_us>\n");
    printf("# LS,<span>,<mean_us>,<std_us>,<us_per_tick_x1000>\n");
    printf("# LR,<slope_x1000>,<intercept_us>,<r2_x10000>\n");
    printf("# B,<iters>,<rep>,<total_ticks>,<inner>\n");
    printf("# BS,<iters>,<mean_ticks_x1000>,<std_ticks_x1000>\n");
    printf("# DRIFT,<pre_x1000>,<post_x1000>,<pct>\n");

    phase1(reps);
    phase2(reps);
    phase3();
    summary();

    printf("# === Done ===\n");
    return 0;
}