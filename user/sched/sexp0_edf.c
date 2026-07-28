/*
 * sexp0_edf.c —— 实验 0: 基线 EDF（α=1，无退避）
 *
 * 目的：建立"无保护"基线，证明自适应 α 的必要性。
 *   - ctrl 拿 ~2/3 CPU（stride 公平，tickets=300:100:50）
 *   - 但 ai 14 线程抢占导致 ctrl 的 burn 被打碎，deadline 大量 miss
 *   - → 需要 α 旋钮在多线程进程和 deadline 进程之间调节
 *
 * 配置（对齐 schedlab edf 默认）:
 *   ctrl: tickets=300, 1线程, in-parent, period=4, burn=400000
 *   ai:   tickets=100, 14线程, spin, burn=12000
 *   log:  tickets=50,  8线程,  spin, burn=12000
 *
 * 用法:
 *   ./sched/sexp0_edf > ./logs/sched/edf/sexp0_edf.csv
 *   python3 ./scripts/sched/stat_exp0.py ./logs/sched/edf/sexp0_edf.csv
 *
 * 通过标准:
 *   - ctrl CPU share ≈ 66%（stride 公平分配）
 *   - ctrl miss rate > 95%（ai 抢占导致 deadline 大量 miss）
 *   - 证明：即使 CPU 分配公平，多线程抢占仍会破坏 deadline → α 必要性
 */
#include "schedlab.h"

static void sl_reset_state(void) {
    sl_ngroups = 0;
    for (int i = 0; i < SL_MAX_GROUPS; i++) {
        sl_gstats[i].work       = 0;
        sl_gstats[i].jobs       = 0;
        sl_gstats[i].miss       = 0;
        sl_gstats[i].late_sum   = 0;
        sl_gstats[i].late_max   = 0;
        sl_gstats[i].resp_sum   = 0;
        sl_gstats[i].resp_sumsq = 0;
        sl_gstats[i].resp_min   = 0;
        sl_gstats[i].resp_max   = 0;
    }
}

static void setup(void) {
    sl_add_jobs_parent("ctrl", 300, 1, /*period*/4, /*cpu*/2, /*burn*/400000);
    sl_add_spin("ai", 100, 14, 12000);
    sl_add_spin("log", 50, 8, 12000);
}

int main(void) {
    const int nreps = 5;
    const unsigned long total = 36000;

    printf("# sexp0_edf: baseline EDF, alpha=1, no backoff\n");
    printf("# reps=%d (1 warmup + %d), total=%lu\n", nreps + 1, nreps, total);
    printf("# ctrl=tickets=300,1thread,period=4,burn=400000\n");
    printf("# ai=tickets=100,14threads,spin,burn=12000\n");
    printf("# log=tickets=50,8threads,spin,burn=12000\n");

    for (int rep = 0; rep <= nreps; rep++) {
        sl_reset_state();
        setup();

        if (rep == 0) {
            printf("# WARMUP rep=0 (discarded)\n");
        } else {
            printf("# RUN rep=%d/%d\n", rep, nreps);
        }

        sl_run(&(sl_cfg){
            .total_ticks  = total,
            .window_ticks = 100,
            .alpha0       = 1,    /* EDF 基线，无退避 */
            .policy       = 0,
            .policy_ud    = 0,
        });
    }

    printf("# sexp0_edf ALL DONE\n");
    return 0;
}
