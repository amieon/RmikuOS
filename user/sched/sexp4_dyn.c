/*
 * sexp4_dyn.c —— 实验 4: 动态负载（三段相位）下 AIMD vs Fixed α
 * v3: 解决"轻不够轻、重不够重"
 *
 * 负载: ctrl(1, period=5, cpu=2, burn=400000)
 *       + ai(150, phased, light_active=3, burn=300000)
 *       + log(32, phased, light_active=4, burn=300000)
 * 相位: 0=轻(ai=3, log=4) → 1=重(ai=150, log=32) → 2=轻
 * 模式: fixed 0/30/60/100 / AIMD 15/50/100
 */
#include "schedlab.h"

static void sl_reset_state(void) {
    sl_ngroups = 0;
    for (int i = 0; i < SL_MAX_GROUPS; i++) {
        sl_group_t *g = &sl_groups[i];
        for (int c = 0; c < SL_NAME_LEN; c++) g->name[c] = 0;
        g->tickets = 0; g->threads = 0; g->kind = 0;
        g->flags = 0; g->light_active = 0;
        g->period_ticks = 0; g->job_cpu_ticks = 0;
        g->burn = 0; g->pid = -1;
        sl_gstats[i].work = 0; sl_gstats[i].jobs = 0; sl_gstats[i].miss = 0;
        sl_gstats[i].late_sum = 0; sl_gstats[i].late_max = 0;
        sl_gstats[i].resp_sum = 0; sl_gstats[i].resp_sumsq = 0;
        sl_gstats[i].resp_min = 0; sl_gstats[i].resp_max = 0;
    }
}

static void setup(void) {
    /* ctrl: period=5, cpu=2 保持中等压力 */
    sl_add_jobs_parent("ctrl", 300, 1, /*period*/7, /*cpu*/2, /*burn*/400000);
    /* ai: burn 加大到 300000 (~1.5 ticks)，轻相位只 3 个活跃 */
    sl_add_spin_phased("ai", 100, 150, /*burn*/300000, /*light_active*/3);
    /* log: 也 phased！轻相位只 4 个活跃，重相位 32 个全上 */
    sl_add_spin_phased("log", 50, 32, /*burn*/300000, /*light_active*/4);
}

static void run_fixed(int alpha, int rep, unsigned long total) {
    sl_reset_state();
    setup();
    printf("# RUN config=dyn mode=fixed alpha=%d rep=%d/3\n", alpha, rep);
    sl_run(&(sl_cfg){
        .total_ticks = total, .window_ticks = 100,
        .alpha0 = alpha, .policy = 0, .policy_ud = 0,
    });
}

static void run_aimd(int alpha0, int rep, unsigned long total) {
    sl_reset_state();
    setup();
    sl_aimd_t aimd;
    sl_aimd_init(&aimd, alpha0);
    printf("# RUN config=dyn mode=aimd alpha0=%d rep=%d/3\n", alpha0, rep);
    sl_run(&(sl_cfg){
        .total_ticks = total, .window_ticks = 100,
        .alpha0 = alpha0, .policy = sl_policy_aimd, .policy_ud = &aimd,
    });
}

int main(void) {
    const unsigned long total = 72000;
    const int nreps = 3;
    const int fixed_alphas[] = {0, 30, 60, 100};
    const int aimd_alphas[]  = {15, 50, 100};

    printf("# sexp4_dyn_v3: dyn load x fixed{0,30,60,100} + aimd{15,50,100} x (1w+%dr), total=%lu\n",
           nreps, total);

    /* fixed baseline */
    for (int fi = 0; fi < 4; fi++) {
        int fa = fixed_alphas[fi];
        printf("\n# === FIXED alpha=%d ===\n", fa);
        for (int r = 0; r <= nreps; r++) {
            if (r == 0) {
                printf("# WARMUP config=dyn fixed%d\n", fa);
                sl_reset_state(); setup();
                printf("# RUN config=dyn mode=fixed alpha=%d rep=0/3\n", fa);
                sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100, .alpha0=fa, .policy=0, .policy_ud=0});
            } else {
                run_fixed(fa, r, total);
            }
        }
    }

    /* AIMD */
    for (int ai = 0; ai < 3; ai++) {
        int a0 = aimd_alphas[ai];
        printf("\n# === AIMD alpha0=%d ===\n", a0);
        for (int r = 0; r <= nreps; r++) {
            if (r == 0) {
                printf("# WARMUP config=dyn aimd%d\n", a0);
                sl_reset_state(); setup();
                sl_aimd_t aimd; sl_aimd_init(&aimd, a0);
                printf("# RUN config=dyn mode=aimd alpha0=%d rep=0/3\n", a0);
                sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100, .alpha0=a0, .policy=sl_policy_aimd, .policy_ud=&aimd});
            } else {
                run_aimd(a0, r, total);
            }
        }
    }

    printf("\n# sexp4_dyn_v3 ALL DONE\n");
    return 0;
}