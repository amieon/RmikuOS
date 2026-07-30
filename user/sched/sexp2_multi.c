/*
 * sexp2_edge.c —— 实验 2: Edge deadline trade-off（全量 5 配置版）
 *
 * 5 个压力等级 × 11 α × (1 warmup + 3 reps) × 6000 tick
 * 预计时长 ~5 小时（1 tick = 13.98 ms，睡前跑早上看结果）
 *
 * 配置（burn=400000 ≈ 2.7 tick, period=4, ctrl fork 子进程）:
 *   light:   ai=7   log=3
 *   medlo:   ai=15  log=8
 *   medium:  ai=25  log=9
 *   heavy:   ai=75  log=25
 *   extreme: ai=225 log=50
 *
 * 用法:
 *   ./sched/sexp2_edge > /tmp/sexp2_edge.csv
 *   python3 ./scripts/sched/stat_exp2_multi.py ./logs/sched/edge/sexp2_edge.csv
 */
#include "schedlab.h"

typedef struct {
    int ai, log;
    const char *name;
} cfg_t;

static const cfg_t CFGS[] = {
    {7,   3, "light"},
    {15,  8, "medlo"},
    {25,  9, "medium"},
    {75, 25, "heavy"},
    {225,50, "extreme"},
};
#define N_CFGS ((int)(sizeof(CFGS)/sizeof(CFGS[0])))

static void sl_reset_state(void) {
    sl_ngroups = 0;
    for (int i = 0; i < SL_MAX_GROUPS; i++) {
        sl_gstats[i].work = 0; sl_gstats[i].jobs = 0; sl_gstats[i].miss = 0;
        sl_gstats[i].late_sum = 0; sl_gstats[i].late_max = 0;
        sl_gstats[i].resp_sum = 0; sl_gstats[i].resp_sumsq = 0;
        sl_gstats[i].resp_min = 0; sl_gstats[i].resp_max = 0;
    }
}

static void setup(int ai, int log) {
    /* ctrl: fork 子进程（非 in-parent），period=4, burn=400k≈2.7tick */
    sl_add_jobs("ctrl", 300, 1, /*period*/4, /*cpu*/3, /*burn*/180000);
    sl_add_spin("ai",  100, ai,  12000);
    sl_add_spin("log",  50, log, 12000);
}

int main(void) {
    static const int alphas[] = {0,10,20,30,40,50,60,70,80,90,100};
    const int nalpha = 11;
    const int nreps  = 5;
    const unsigned long total = 6000;

    printf("# sexp2_edge: %d configs x %d alphas x (1w+%dr), total=%lu\n",
           N_CFGS, nalpha, nreps, total);
    printf("# ctrl=300tk,1t,p=4,burn=400k(fork)  burn≈2.7tick\n");

    for (int ci = 0; ci < N_CFGS; ci++) {
        const cfg_t *c = &CFGS[ci];
        printf("\n# === CONFIG %s ai=%d log=%d ===\n", c->name, c->ai, c->log);

        for (int ai = 0; ai < nalpha; ai++) {
            int a = alphas[ai];

            /* warmup */
            sl_reset_state();
            setup(c->ai, c->log);
            printf("# WARMUP config=%s alpha=%d\n", c->name, a);
            sl_run(&(sl_cfg){
                .total_ticks = total, .window_ticks = 100,
                .alpha0 = a, .policy = 0, .policy_ud = 0,
                /* extreme(225线程)创建慢，给 200 tick */
                .start_delay = (c->ai > 100) ? 200 : 80,
            });

            /* reps */
            for (int rep = 1; rep <= nreps; rep++) {
                sl_reset_state();
                setup(c->ai, c->log);
                printf("# RUN config=%s alpha=%d rep=%d/%d\n", c->name, a, rep, nreps);
                sl_run(&(sl_cfg){
                    .total_ticks = total, .window_ticks = 100,
                    .alpha0 = a, .policy = 0, .policy_ud = 0,
                    .start_delay = (c->ai > 100) ? 200 : 80,
                });
            }
        }
    }

    printf("\n# sexp2_edge ALL DONE\n");
    return 0;
}
