/*
 * sexp9_ucb.c —— 实验 9: SW-UCB 滑动窗口多臂老虎机
 *                (Garivier & Moulines 2011 思路)
 *
 * 定位: 与前八个实验完全不同的方法家族 —— 离散在线学习/探索-利用。
 *   α 不再连续调节, 而是离散成 11 个臂(0,10,...,100), 每窗整臂调度,
 *   reward = 10000 − loss(与 AdamW 同一 loss 函数)。
 *   选臂: mean_i + c·sqrt(2·ln t / n_i), 每臂只记最近 τ=32 个样本
 *   (滑窗: 纯 UCB1 的全历史平均会被相位切换前的旧数据污染)。
 *
 * 负载配置同 exp4/5/6/7/8(一个字不改), 3 ratios(500/800/200)。
 * mode: ucb(α0=50 仅作首窗起始臂=5; 冷启动逐臂扫一遍后无起点记忆,
 *   故无 ucb0/ucb100 变体 —— 起点对这个控制器结构上无意义)
 *
 * 先验(可证伪, 写进报告的诚实局限):
 *   - bandit 假设 reward 只依赖所选臂; 实际还依赖相位上下文
 *     → 无上下文的 UCB 在相位负载下天然吃亏, 落后本身是结论
 *   - 预期 ratio=200(H 段占 80%)时 UCB 最不吃亏: 环境最接近平稳
 *   - c_explore=1000 与 loss 量纲(0..5000)匹配, 数据回来先看
 *     各臂被选次数分布, 若探索不足/过度再调
 *
 * 3 ratios × 1 mode × (1 warmup + 3 reps) = 12 runs × 56min ≈ 11h
 *
 * 用法:
 *   ./sched/sexp9_ucb > /tmp/sexp9_ucb.csv
 *   (A 行 action=armN, N 为所选臂号 —— 统计时按 arm 分布分析)
 */
#include "schedlab.h"

static void sl_reset_state(void) {
    sl_ngroups = 0;
    for (int i = 0; i < SL_MAX_GROUPS; i++) {
        sl_gstats[i].work = 0; sl_gstats[i].jobs = 0; sl_gstats[i].miss = 0;
        sl_gstats[i].late_sum = 0; sl_gstats[i].late_max = 0;
        sl_gstats[i].resp_sum = 0; sl_gstats[i].resp_sumsq = 0;
        sl_gstats[i].resp_min = 0; sl_gstats[i].resp_max = 0;
    }
}

static void setup(void) {
    sl_add_jobs_parent("ctrl", 300, 1, /*period*/5, /*cpu*/3, /*burn*/180000);
    sl_add_spin_phased("ai", 100, 50, 12000, /*light_active*/5);
    sl_add_spin("log", 50, 3, 12000);
}

/* sl_ucb_t 约 3.2KB(11 臂 × 32 样本环形缓冲), 必须 .bss 静态分配,
 * 放栈上有爆用户栈风险(框架头文件 .bss 教训同理)。 */
static sl_ucb_t g_ucb;

static void run_ucb(int l_ratio, int rep /* 0=warmup */, unsigned long total) {
    sl_reset_state();
    setup();
    sl_l_ratio_permil = l_ratio;

    sl_ucb_init(&g_ucb, /*alpha0*/50);

    if (rep == 0)
        printf("# WARMUP ratio=%d mode=ucb alpha0=50\n", l_ratio);
    else
        printf("# RUN ratio=%d mode=ucb alpha0=50 rep=%d/3\n", l_ratio, rep);

    sl_run(&(sl_cfg){
        .total_ticks = total,
        .window_ticks = 100,
        .alpha0 = 50,
        .policy = sl_policy_ucb,
        .policy_ud = &g_ucb,
    });

    sl_l_ratio_permil = 0;
}

int main(void) {
    const unsigned long total = 240000;
    const int nreps = 3;

    struct { int ratio; const char *name; } ratios[] = {
        {500, "25/25"},
        {800, "40/10"},
        {200, "10/40"},
    };
    const int nratios = 3;

    printf("# sexp9_ucb: sliding-window UCB, %d ratios x 1 mode x %d reps, total=%lu\n",
           nratios, nreps, total);
    printf("# ctrl=300tk,1t,p=5,burn=180k(in-parent)\n");
    printf("# ai=100tk,50t,phased(light_active=5)  log=50tk,3t,spin\n");
    printf("# ucb: 11 arms, tau=32, c_explore=1000, reward=10000-loss\n");

    for (int ri = 0; ri < nratios; ri++) {
        printf("\n# === RATIO %s (permil=%d) ===\n", ratios[ri].name, ratios[ri].ratio);

        run_ucb(ratios[ri].ratio, /*warmup*/0, total);
        for (int r = 1; r <= nreps; r++) {
            run_ucb(ratios[ri].ratio, r, total);
        }
    }

    printf("\n# sexp9_ucb ALL DONE\n");
    return 0;
}
