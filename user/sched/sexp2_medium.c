/*
 * sexp2_medium.c —— 实验 2: Edge deadline trade-off (medium 配置)
 *
 * 关键改动：ctrl 用 sl_add_jobs（fork 子进程），不用 sl_add_jobs_parent。
 * exp2 是固定 α 扫描，不需要 in-parent 的 D 行控制器反馈。
 * fork 子进程的 ctrl 不被监控主线程打断，方差大幅降低。
 *
 * period=10：burn 需 ~1.5 tick，10 tick 给 8.5 tick 余量(α=0)，
 * α=100 时 ctrl share~9%，10×0.09=0.9 tick < 1.5，miss 上升。
 *
 * 用法:
 *   ./sched/sexp2_medium > /tmp/sexp2_medium.csv
 *   python3 ./scripts/sched/stat_exp2_multi.py ./logs/sched/edge/sexp2_medium.csv
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
    /* ctrl 用 fork 子进程（非 in-parent），避免被主线程打断 */
    sl_add_jobs("ctrl", 300, 1, /*period*/10, /*cpu*/3, /*burn*/400000);
    sl_add_spin("ai",  100, 25, 12000);
    sl_add_spin("log",  50,  9, 12000);
}

int main(void) {
    static const int alphas[] = {0,10,20,30,40,50,60,70,80,90,100};
    const int nalpha = 11;
    const int nreps  = 5;
    const unsigned long total = 6000;

    printf("# sexp2_medium: medium config, %d alphas x (1w+%dr), total=%lu\n",
           nalpha, nreps, total);
    printf("# ctrl=300tk,1t,p=10,burn=400k(fork)  ai=100tk,25t  log=50tk,9t\n");

    for (int ai = 0; ai < nalpha; ai++) {
        int a = alphas[ai];

        /* warmup */
        sl_reset_state();
        setup();
        printf("# WARMUP config=medium alpha=%d\n", a);
        sl_run(&(sl_cfg){
            .total_ticks = total, .window_ticks = 100,
            .alpha0 = a, .policy = 0, .policy_ud = 0,
        });

        /* reps */
        for (int rep = 1; rep <= nreps; rep++) {
            sl_reset_state();
            setup();
            printf("# RUN config=medium alpha=%d rep=%d/%d\n", a, rep, nreps);
            sl_run(&(sl_cfg){
                .total_ticks = total, .window_ticks = 100,
                .alpha0 = a, .policy = 0, .policy_ud = 0,
            });
        }
    }

    printf("\n# sexp2_medium ALL DONE\n");
    return 0;
}