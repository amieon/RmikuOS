/*
 * sexp5_phase.c —— 实验 5: 相位比例对 AIMD 优势的影响
 *
 * 固定负载配置(ctrl/ai/log 同 exp4),变化 L/H 相位比例:
 *   ratio=800: 40/10/40/10 (L占80%,AIMD有利)
 *   ratio=200: 10/40/10/40 (H占80%,AIMD不利)
 *   (25/25/25/25 等分已在 exp4 测过,不重复)
 *
 * 目的: 验证"L段占比越大,AIMD 相对 fixed 的吞吐量优势越大"
 *   - L段: AIMD α冲高 vs fixed25 α=25 → AIMD 拉开差距
 *   - H段: AIMD α退避≈25 vs fixed25 α=25 → 差不多
 *   - L段越多 → AIMD 优势越大
 *
 * 8 modes: fixed0/25/50/75/100 + aimd0/50/100
 * 2 ratios × 8 modes × 3 reps = 48 runs
 *
 * 用法:
 *   ./sched/sexp5_phase > /tmp/sexp5_phase.csv
 *   python3 ./scripts/sched/stat_exp5.py ./logs/sched/phase/sexp5_phase.csv
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
    /* ctrl: in-parent jobs, period=5, burn=180k */
    sl_add_jobs_parent("ctrl", 300, 1, /*period*/5, /*cpu*/3, /*burn*/180000);
    /* ai: phased spin, 50 线程, 轻相位 5 个活跃 */
    sl_add_spin_phased("ai", 100, 50, 12000, /*light_active*/5);
    /* log: 3 线程 */
    sl_add_spin("log", 50, 3, 12000);
}

static void run_mode(int l_ratio, const char *mode, int alpha0, int is_aimd,
                     int rep, unsigned long total) {
    sl_reset_state();
    setup();

    sl_aimd_t aimd;
    if (is_aimd) {
        sl_aimd_init(&aimd, alpha0);
        aimd.danger_lateness = 25;
        aimd.safe_lateness = 0;
        /* inc=5 (sl_aimd_init 默认), cooldown=3/safe>=2 在 sl_policy_aimd 里 */
    }

    /* 设置相位比例 */
    sl_l_ratio_permil = l_ratio;

    printf("# RUN ratio=%d mode=%s alpha0=%d rep=%d/3\n",
           l_ratio, mode, alpha0, rep);
    sl_run(&(sl_cfg){
        .total_ticks = total,
        .window_ticks = 100,
        .alpha0 = alpha0,
        .policy = is_aimd ? sl_policy_aimd : 0,
        .policy_ud = is_aimd ? &aimd : 0,
    });

    /* 恢复默认,避免影响后续非 phased 实验 */
    sl_l_ratio_permil = 0;
}

int main(void) {
    const unsigned long total = 240000;
    const int nreps = 3;

    /* 2 个相位比例 (25/25 在 exp4 已测) */
    struct { int ratio; const char *name; } ratios[] = {
        {800, "40/10"},
        {200, "10/40"},
    };
    const int nratios = 2;

    /* 8 modes */
    struct { const char *mode; int alpha0; int is_aimd; } modes[] = {
        {"fixed0",   0, 0},
        {"fixed25", 25, 0},
        {"fixed50", 50, 0},
        {"fixed75", 75, 0},
        {"fixed100",100, 0},
        {"aimd0",    0, 1},
        {"aimd50",  50, 1},
        {"aimd100",100, 1},
    };
    const int nmodes = 8;

    printf("# sexp5_phase: phase ratio experiment, %d ratios x %d modes x %d reps, total=%lu\n",
           nratios, nmodes, nreps, total);
    printf("# ctrl=300tk,1t,p=5,burn=180k(in-parent)\n");
    printf("# ai=100tk,50t,phased(light_active=5)  log=50tk,3t,spin\n");
    printf("# ratios: 800=40/10/40/10, 200=10/40/10/40 (25/25 in exp4)\n");

    for (int ri = 0; ri < nratios; ri++) {
        printf("\n# === RATIO %s (permil=%d) ===\n", ratios[ri].name, ratios[ri].ratio);

        for (int mi = 0; mi < nmodes; mi++) {
            /* warmup */
            printf("# WARMUP ratio=%d mode=%s alpha0=%d\n",
                   ratios[ri].ratio, modes[mi].mode, modes[mi].alpha0);
            sl_reset_state(); setup();
            sl_aimd_t aimd_w;
            if (modes[mi].is_aimd) {
                sl_aimd_init(&aimd_w, modes[mi].alpha0);
                aimd_w.danger_lateness = 25;
                aimd_w.safe_lateness = 0;
            }
            sl_l_ratio_permil = ratios[ri].ratio;
            sl_run(&(sl_cfg){
                .total_ticks = total, .window_ticks = 100,
                .alpha0 = modes[mi].alpha0,
                .policy = modes[mi].is_aimd ? sl_policy_aimd : 0,
                .policy_ud = modes[mi].is_aimd ? &aimd_w : 0,
            });
            sl_l_ratio_permil = 0;

            /* formal reps */
            for (int r = 1; r <= nreps; r++) {
                run_mode(ratios[ri].ratio, modes[mi].mode, modes[mi].alpha0,
                         modes[mi].is_aimd, r, total);
            }
        }
    }

    printf("\n# sexp5_phase ALL DONE\n");
    return 0;
}
