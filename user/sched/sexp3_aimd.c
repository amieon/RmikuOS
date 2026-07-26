/*
 * sexp3_aimd_resume.c —— 续跑：medium aimd100 + heavy 全配置
 * 追加到原 CSV:  tee -a ./logs/sched/aimd/sexp3_aimd.csv
 */
#include "schedlab.h"

typedef struct { int ai, log; const char *name; } cfg_t;

static void sl_reset_state(void) {
    sl_ngroups = 0;
    for (int i = 0; i < SL_MAX_GROUPS; i++) {
        sl_gstats[i].work = 0; sl_gstats[i].jobs = 0; sl_gstats[i].miss = 0;
        sl_gstats[i].late_sum = 0; sl_gstats[i].late_max = 0;
        sl_gstats[i].resp_sum = 0; sl_gstats[i].resp_sumsq = 0;
        sl_gstats[i].resp_min = 0; sl_gstats[i].resp_max = 0;
    }
}

static void setup(const cfg_t *c) {
    sl_add_jobs_parent("ctrl", 300, 1, 10, 3, 400000);
    sl_add_spin("ai",  100, c->ai,  12000);
    sl_add_spin("log",  50, c->log, 12000);
}

static void run_fixed(const cfg_t *c, int rep, unsigned long total) {
    sl_reset_state(); setup(c);
    printf("# RUN config=%s mode=fixed alpha=50 rep=%d/5\n", c->name, rep);
    sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100, .alpha0=50, .policy=0, .policy_ud=0});
}

static void run_aimd(const cfg_t *c, int alpha0, int rep, unsigned long total) {
    sl_reset_state(); setup(c);
    sl_aimd_t aimd; sl_aimd_init(&aimd, alpha0);
    printf("# RUN config=%s mode=aimd alpha0=%d rep=%d/5\n", c->name, alpha0, rep);
    sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100, .alpha0=alpha0, .policy=sl_policy_aimd, .policy_ud=&aimd});
}

int main(void) {
    const unsigned long total = 36000;
    const int nreps = 5;

    /* === 续跑 medium aimd100 === */
    printf("# === RESUME: medium aimd100 ===\n");
    for (int r = 0; r <= nreps; r++) {
        if (r == 0) {
            printf("# WARMUP config=medium aimd100\n");
            sl_reset_state();
            sl_add_jobs_parent("ctrl", 300, 1, 10, 3, 400000);
            sl_add_spin("ai", 100, 25, 12000);
            sl_add_spin("log", 50, 9, 12000);
            sl_aimd_t aimd; sl_aimd_init(&aimd, 100);
            printf("# RUN config=medium mode=aimd alpha0=100 rep=0/5\n");
            sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100, .alpha0=100, .policy=sl_policy_aimd, .policy_ud=&aimd});
        } else {
            cfg_t medium = {25, 9, "medium"};
            run_aimd(&medium, 100, r, total);
        }
    }

    /* === heavy 全配置 === */
    printf("\n# === heavy ===\n");
    cfg_t heavy = {75, 25, "heavy"};

    printf("# --- fixed 50 ---\n");
    for (int r = 0; r <= nreps; r++) {
        if (r == 0) {
            printf("# WARMUP config=heavy fixed50\n");
            sl_reset_state(); setup(&heavy);
            printf("# RUN config=heavy mode=fixed alpha=50 rep=0/5\n");
            sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100, .alpha0=50, .policy=0, .policy_ud=0});
        } else {
            run_fixed(&heavy, r, total);
        }
    }

    for (int a0 = 0; a0 <= 100; a0 += 50) {  /* 0, 50, 100 */
        printf("# --- aimd alpha0=%d ---\n", a0);
        for (int r = 0; r <= nreps; r++) {
            if (r == 0) {
                printf("# WARMUP config=heavy aimd%d\n", a0);
                sl_reset_state(); setup(&heavy);
                sl_aimd_t aimd; sl_aimd_init(&aimd, a0);
                printf("# RUN config=heavy mode=aimd alpha0=%d rep=0/5\n", a0);
                sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100, .alpha0=a0, .policy=sl_policy_aimd, .policy_ud=&aimd});
            } else {
                run_aimd(&heavy, a0, r, total);
            }
        }
    }

    printf("\n# sexp3_aimd_resume ALL DONE\n");
    return 0;
}