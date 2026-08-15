/*
 * sexp8_pid.c —— 实验 8: 增量式 PI 控制器(控制论经典, 对照靶子)
 *
 * 定位: PID 在资源调度文献里已充分研究(control-theoretic scheduling),
 *   本实验不是创新点, 是对照组 —— "教科书控制器 vs 领域启发式(AIMD)"
 *   谁更适合 deadline 场景。输了是"无死区连续调节在此噪声下不适合",
 *   赢了是"经典控制直接够用", 两头都有结论。
 *
 * 控制器(实现见 schedlab.h 的 sl_policy_pid):
 *   e = late_delta − target(10)
 *   Δα = −( kp·Δe + ki·e )/1024, α 即积分器, [0,100] 钳位 = anti-windup
 *   kp_fp=150: 相位切换 Δe≈90 → 首窗猛降 ~13 点(阻尼项)
 *   ki_fp=64:  重相位 e≈90 → 持续 −5.6 点/窗; 轻相位 e=−10 → +0.6/窗慢爬
 *
 * 负载配置同 exp4/5/6/7(一个字不改), 3 ratios(500/800/200)。
 * modes: pid0(从安全区起点) / pid100(从灾难区起点, 看 windup 恢复)
 *
 * 先验(可证伪):
 *   - 恒定段(相位内部)比 AIMD 平滑: 连续调节无死区, α 无台阶
 *   - 相位切换后恢复慢于 AIMD: Δe 项反向拖拽(e_prev 惯性) +
 *     无显式退避机制, 靠 ki 项一窗 5.6 点慢慢压
 *   - 轻相位爬升 0.6 点/窗 << AIMD 的 +5/窗 → L 段吞吐吃亏
 *     (ratio=800 时 AIMD 优势档, 预期 PID 落后最多)
 *
 * 3 ratios × 2 modes × (1 warmup + 3 reps) = 24 runs × 56min ≈ 22h
 *
 * 用法:
 *   ./sched/sexp8_pid > /tmp/sexp8_pid.csv
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

static sl_pid_t g_pid;

static void run_pid(int l_ratio, const char *mode, int alpha0,
                    int rep /* 0=warmup */, unsigned long total) {
    sl_reset_state();
    setup();
    sl_l_ratio_permil = l_ratio;

    /* kp=150, ki=64(均 ×1024 定点), target=10 —— 标定推导见 schedlab.h */
    sl_pid_init(&g_pid, alpha0, /*kp_fp*/150, /*ki_fp*/64, /*target*/10);

    if (rep == 0)
        printf("# WARMUP ratio=%d mode=%s alpha0=%d\n", l_ratio, mode, alpha0);
    else
        printf("# RUN ratio=%d mode=%s alpha0=%d rep=%d/3\n",
               l_ratio, mode, alpha0, rep);

    sl_run(&(sl_cfg){
        .total_ticks = total,
        .window_ticks = 100,
        .alpha0 = alpha0,
        .policy = sl_policy_pid,
        .policy_ud = &g_pid,
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

    struct { const char *mode; int alpha0; } modes[] = {
        {"pid0",     0},
        {"pid100", 100},
    };
    const int nmodes = 2;

    printf("# sexp8_pid: incremental PI controller, %d ratios x %d modes x %d reps, total=%lu\n",
           nratios, nmodes, nreps, total);
    printf("# ctrl=300tk,1t,p=5,burn=180k(in-parent)\n");
    printf("# ai=100tk,50t,phased(light_active=5)  log=50tk,3t,spin\n");
    printf("# pid: kp=150, ki=64 (fp1024), target=10, clamp anti-windup\n");

    for (int ri = 0; ri < nratios; ri++) {
        printf("\n# === RATIO %s (permil=%d) ===\n", ratios[ri].name, ratios[ri].ratio);

        for (int mi = 0; mi < nmodes; mi++) {
            run_pid(ratios[ri].ratio, modes[mi].mode, modes[mi].alpha0,
                    /*warmup*/0, total);
            for (int r = 1; r <= nreps; r++) {
                run_pid(ratios[ri].ratio, modes[mi].mode, modes[mi].alpha0,
                        r, total);
            }
        }
    }

    printf("\n# sexp8_pid ALL DONE\n");
    return 0;
}
