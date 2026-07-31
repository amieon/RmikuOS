/*
 * sexp4_dyn.c —— 实验 4: 动态负载 AIMD vs 固定 α
 *
 * 负载模式:轻重轻重四段(spin phased)
 *   - 轻相位:ai 只有 light_active 个线程活跃
 *   - 重相位:ai 全部线程活跃
 *
 * 对比:
 *   fixed0    (最保守,ctrl 最好但 ai 被压)
 *   fixed50   (折中,重相位可能崩)
 *   aimd0     (从安全区启动)
 *   aimd50    (从中间启动)
 *
 * ctrl 用 in-parent jobs(D 行反馈),ai 用 phased spin(fork)
 *
 * 用法:
 *   ./sched/sexp4_dyn > /tmp/sexp4_dyn.csv
 *   python3 ./scripts/sched/stat_exp4.py ./logs/sched/dyn/sexp4_dyn.csv
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
    /* ctrl: in-parent jobs, period=5, burn=180k≈1.2tick */
    sl_add_jobs_parent("ctrl", 300, 1, /*period*/5, /*cpu*/3, /*burn*/180000);
    /* ai: phased spin, 25 线程, 轻相位只 3 个活跃 */
    sl_add_spin_phased("ai", 100, 25, 12000, /*light_active*/3);
    /* log: 普通 spin */
    sl_add_spin("log", 50, 9, 12000);
}

static void run_mode(const char *mode, int alpha0, int is_aimd, int rep,
                     unsigned long total) {
    sl_reset_state();
    setup();

    sl_aimd_t aimd;
    if (is_aimd) {
        sl_aimd_init(&aimd, alpha0);
        /* 和 exp3 一致的参数 */
        aimd.danger_lateness = 25;
        aimd.safe_lateness = 0;
        aimd.inc = 1; 
    }

    printf("# RUN mode=%s alpha0=%d rep=%d/3\n", mode, alpha0, rep);
    sl_run(&(sl_cfg){
        .total_ticks = total,
        .window_ticks = 100,
        .alpha0 = alpha0,
        .policy = is_aimd ? sl_policy_aimd : 0,
        .policy_ud = is_aimd ? &aimd : 0,
    });
}

int main(void) {
    const unsigned long total = 96000;
    const int nreps = 3;

    printf("# sexp4_dyn: light-heavy-light-heavy, %d modes x (1w+%dr), total=%lu\n",
           8, nreps, total);
    printf("# ctrl=300tk,1t,p=5,burn=180k(in-parent)\n");
    printf("# ai=100tk,25t,phased(light_active=3)  log=50tk,9t,spin\n");
    printf("# phases: L(0-25%%) H(25-50%%) L(50-75%%) H(75-100%%)\n");

    /* 8 modes: fixed0/25/50/75/100 + aimd0/50/100 */
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

    for (int mi = 0; mi < nmodes; mi++) {
        /* warmup */
        printf("# WARMUP mode=%s alpha0=%d\n", modes[mi].mode, modes[mi].alpha0);
        sl_reset_state(); setup();
        sl_aimd_t aimd_w;
        if (modes[mi].is_aimd) {
            sl_aimd_init(&aimd_w, modes[mi].alpha0);
            aimd_w.danger_lateness = 25;
            aimd_w.safe_lateness = 0;
            aimd_w.inc = 1; 
        }
        sl_run(&(sl_cfg){
            .total_ticks = total, .window_ticks = 100,
            .alpha0 = modes[mi].alpha0,
            .policy = modes[mi].is_aimd ? sl_policy_aimd : 0,
            .policy_ud = modes[mi].is_aimd ? &aimd_w : 0,
        });

        /* reps */
        for (int r = 1; r <= nreps; r++) {
            run_mode(modes[mi].mode, modes[mi].alpha0, modes[mi].is_aimd, r, total);
        }
    }

    printf("\n# sexp4_dyn ALL DONE\n");
    return 0;
}
