/*
 * sexp6_adamw.c —— 实验 6: SPSA-AdamW 自适应控制器
 *
 * 负载配置同 exp4/exp5,3 种相位比例全测:
 *   ratio=250: 25/25/25/25 (等分)
 *   ratio=800: 40/10/40/10 (L占80%)
 *   ratio=200: 10/40/10/40 (H占80%)
 *
 * 对比:
 *   adamw0/50/100: SPSA-AdamW, 3 个起点, lr=3, target=25
 *   (AIMD/fixed 数据复用 exp4/exp5, stat 脚本合并)
 *
 * 目的: 对比"梯度优化(AdamW)" vs "启发式规则(AIMD)"在调度场景的表现
 *   - AdamW 能不能像 AIMD 一样不崩? (loss 高→梯度正→降 α)
 *   - AdamW 能不能像 AIMD 一样冲高? (loss=0→梯度=0→无信号爬高?)
 *   - 哪种控制器更适合调度问题?
 *
 * 3 ratios × 3 adamw modes × 3 reps = 27 runs
 *
 * 用法:
 *   ./sched/sexp6_adamw > /tmp/sexp6_adamw.csv
 *   python3 ./scripts/sched/stat_exp6.py \
 *     ./logs/sched/adamw/sexp6_adamw.csv \
 *     ./logs/sched/phase/sexp5_phase.csv \
 *     ./logs/sched/dyn/sexp4_dyn.csv
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

static void run_adamw(int l_ratio, const char *mode, int alpha0,
                      int rep, unsigned long total) {
    sl_reset_state();
    setup();

    sl_adamw_t adamw;
    /* lr=3: 步长适中(每 window ~3 alpha 点)
     * target=25: weight decay 目标(中度保守,拉向 edge 附近)
     * delta=5: SPSA 扰动幅度(sl_adamw_init 默认) */
    sl_adamw_init(&adamw, alpha0, /*lr*/3, /*target*/25);

    sl_l_ratio_permil = l_ratio;

    printf("# RUN ratio=%d mode=%s alpha0=%d rep=%d/3\n",
           l_ratio, mode, alpha0, rep);
    sl_run(&(sl_cfg){
        .total_ticks = total,
        .window_ticks = 100,
        .alpha0 = alpha0,
        .policy = sl_policy_adamw,
        .policy_ud = &adamw,
    });

    sl_l_ratio_permil = 0;
}

int main(void) {
    const unsigned long total = 96000;
    const int nreps = 3;

    struct { int ratio; const char *name; } ratios[] = {
        {250, "25/25"},
        {800, "40/10"},
        {200, "10/40"},
    };
    const int nratios = 3;

    struct { const char *mode; int alpha0; } adamw_modes[] = {
        {"adamw0",    0},
        {"adamw50",  50},
        {"adamw100",100},
    };
    const int nmodes = 3;

    printf("# sexp6_adamw: SPSA-AdamW, %d ratios x %d modes x %d reps, total=%lu\n",
           nratios, nmodes, nreps, total);
    printf("# ctrl=300tk,1t,p=5,burn=180k(in-parent)\n");
    printf("# ai=100tk,50t,phased(light_active=5)  log=50tk,3t,spin\n");
    printf("# adamw: lr=3, target=25, delta=5(SPSA)\n");

    for (int ri = 0; ri < nratios; ri++) {
        printf("\n# === RATIO %s (permil=%d) ===\n", ratios[ri].name, ratios[ri].ratio);

        for (int mi = 0; mi < nmodes; mi++) {
            /* warmup */
            printf("# WARMUP ratio=%d mode=%s alpha0=%d\n",
                   ratios[ri].ratio, adamw_modes[mi].mode, adamw_modes[mi].alpha0);
            sl_reset_state(); setup();
            sl_adamw_t aw_w;
            sl_adamw_init(&aw_w, adamw_modes[mi].alpha0, 3, 25);
            sl_l_ratio_permil = ratios[ri].ratio;
            sl_run(&(sl_cfg){
                .total_ticks = total, .window_ticks = 100,
                .alpha0 = adamw_modes[mi].alpha0,
                .policy = sl_policy_adamw,
                .policy_ud = &aw_w,
            });
            sl_l_ratio_permil = 0;

            /* formal reps */
            for (int r = 1; r <= nreps; r++) {
                run_adamw(ratios[ri].ratio, adamw_modes[mi].mode,
                          adamw_modes[mi].alpha0, r, total);
            }
        }
    }

    printf("\n# sexp6_adamw ALL DONE\n");
    return 0;
}
 