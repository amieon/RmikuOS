/*
 * sexp2_edge.c —— 实验 2: Edge deadline trade-off 全量扫描
 *
 * 负载:  ctrl(1线程,300票,period=10) / ai(25线程,100票) / log(9线程,50票)
 * 扫描:  α ∈ {0,10,...,100} × (1 warmup + 3 reps) = 44 次 sl_run
 * 膝点预期: α*≈50-55(period=10 下 ctrl 份额跌破 30% 即 miss)
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

static void setup_edge(void) {
    /* ctrl: 1 线程, 300 票, period=10, burn≈3 ticks CPU */
    sl_add_jobs_parent("ctrl", 300, 1, /*period*/10, /*cpu*/3, /*burn*/400000);
    /* ai: 25 线程, 100 票, α 放大器 */
    sl_add_spin("ai",  100, 25, 12000);
    /* log: 9 线程, 50 票 */
    sl_add_spin("log",  50,  9, 12000);
}

int main(void) {
    static const int alphas[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    const int nalpha = 11;
    const int nreps  = 3;
    const unsigned long total = 3600;

    printf("# sexp2_edge: %d alphas x (1 warmup + %d reps), total=%lu\n",
           nalpha, nreps, total);
    printf("# config: ctrl(1t,300tk,p=10) ai(25t,100tk) log(9t,50tk)\n");

    for (int ai = 0; ai < nalpha; ai++) {
        int a = alphas[ai];

        /* warmup(丢弃) */
        sl_reset_state();
        setup_edge();
        printf("# WARMUP alpha=%d\n", a);
        sl_run(&(sl_cfg){
            .total_ticks  = total,
            .window_ticks = 100,
            .alpha0       = a,
            .policy       = 0,
            .policy_ud    = 0,
        });

        /* 正式 reps */
        for (int rep = 1; rep <= nreps; rep++) {
            sl_reset_state();
            setup_edge();
            printf("# RUN alpha=%d rep=%d/%d\n", a, rep, nreps);
            sl_run(&(sl_cfg){
                .total_ticks  = total,
                .window_ticks = 100,
                .alpha0       = a,
                .policy       = 0,
                .policy_ud    = 0,
            });
        }
    }

    printf("# sexp2_edge ALL DONE\n");
    return 0;
}