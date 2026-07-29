/*
 * sexp2_multi.c —— 实验 2: 多配置 Edge trade-off 扫描
 *
 * 5 个压力等级:
 *   light:   ctrl=1 ai=7   log=3
 *   medlo:   ctrl=1 ai=15  log=8
 *   medium:  ctrl=1 ai=25  log=9   (原配置)
 *   heavy:   ctrl=1 ai=75  log=25
 *   extreme: ctrl=1 ai=225 log=50
 *
 * 每个配置: α=0..100 step=10 × (1 warmup + 3 reps)
 */
#include "schedlab.h"

typedef struct {
    int ai, log;
    const char *name;
} cfg_t;

static const cfg_t CFGS[] = {

    {25,  9, "medium"},

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
    sl_add_jobs_parent("ctrl", 300, 1, /*period*/20, /*cpu*/3, /*burn*/400000);
    sl_add_spin("ai",  100, ai,  12000);
    sl_add_spin("log",  50, log, 12000);
}

int main(void) {
    static const int alphas[] = {0,10,20,30,40,50,60,70,80,90,100};
    const int nalpha = 11;
    const int nreps  = 3;
    const unsigned long total = 6000;

    printf("# sexp2_multi: %d configs x %d alphas x (1w+%dr), total=%lu\n",
           N_CFGS, nalpha, nreps, total);

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
            });

            /* reps */
            for (int rep = 1; rep <= nreps; rep++) {
                sl_reset_state();
                setup(c->ai, c->log);
                printf("# RUN config=%s alpha=%d rep=%d/%d\n", c->name, a, rep, nreps);
                sl_run(&(sl_cfg){
                    .total_ticks = total, .window_ticks = 100,
                    .alpha0 = a, .policy = 0, .policy_ud = 0,
                });
            }
        }
    }

    printf("\n# sexp2_multi ALL DONE\n");
    return 0;
}