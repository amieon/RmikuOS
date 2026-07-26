/*
 * sexp3_aimd.c —— 实验 3: AIMD 恒定负载 + 多配置 + 多初始 α
 *
 * 4 个配置: light/medlo/medium/heavy
 * 每个配置:
 *   fixed 50 baseline × 5 reps
 *   AIMD α0=0  × 5 reps  (从安全区启动，验证不发散)
 *   AIMD α0=50 × 5 reps  (从膝点附近启动，验证稳态)
 *   AIMD α0=100 × 5 reps (从灾难区启动，验证收敛)
 * 总时长: 36000 ticks
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
};
#define N_CFGS 4

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
    sl_add_jobs_parent("ctrl", 300, 1, /*period*/10, /*cpu*/3, /*burn*/400000);
    sl_add_spin("ai",  100, c->ai,  12000);
    sl_add_spin("log",  50, c->log, 12000);
}

static void run_fixed(const cfg_t *c, int rep, unsigned long total) {
    sl_reset_state();
    setup(c);
    printf("# RUN config=%s mode=fixed alpha=50 rep=%d/5\n", c->name, rep);
    sl_run(&(sl_cfg){
        .total_ticks = total, .window_ticks = 100,
        .alpha0 = 50, .policy = 0, .policy_ud = 0,
    });
}

static void run_aimd(const cfg_t *c, int alpha0, int rep, unsigned long total) {
    sl_reset_state();
    setup(c);
    sl_aimd_t aimd;
    sl_aimd_init(&aimd, alpha0);
    printf("# RUN config=%s mode=aimd alpha0=%d rep=%d/5\n", c->name, alpha0, rep);
    sl_run(&(sl_cfg){
        .total_ticks = total, .window_ticks = 100,
        .alpha0 = alpha0, .policy = sl_policy_aimd, .policy_ud = &aimd,
    });
}

int main(void) {
    const unsigned long total = 36000;
    const int nreps = 5;
    const int aimd_alphas[] = {0, 50, 100};

    printf("# sexp3_aimd: %d configs x (fixed50 + aimd0/50/100) x (1w+%dr), total=%lu\n",
           N_CFGS, nreps, total);

    for (int ci = 0; ci < N_CFGS; ci++) {
        const cfg_t *c = &CFGS[ci];
        printf("\n# === CONFIG %s ai=%d log=%d ===\n", c->name, c->ai, c->log);

        /* fixed 50 baseline */
        printf("# --- fixed 50 ---\n");
        for (int r = 0; r <= nreps; r++) {
            if (r == 0) {
                printf("# WARMUP config=%s fixed50\n", c->name);
                sl_reset_state(); setup(c);
                printf("# RUN config=%s mode=fixed alpha=50 rep=0/5\n", c->name);
                sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100, .alpha0=50, .policy=0, .policy_ud=0});
            } else {
                run_fixed(c, r, total);
            }
        }

        /* AIMD from 0, 50, 100 */
        for (int ai = 0; ai < 3; ai++) {
            int a0 = aimd_alphas[ai];
            printf("# --- aimd alpha0=%d ---\n", a0);
            for (int r = 0; r <= nreps; r++) {
                if (r == 0) {
                    printf("# WARMUP config=%s aimd%d\n", c->name, a0);
                    sl_reset_state(); setup(c);
                    sl_aimd_t aimd; sl_aimd_init(&aimd, a0);
                    printf("# RUN config=%s mode=aimd alpha0=%d rep=0/5\n", c->name, a0);
                    sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100, .alpha0=a0, .policy=sl_policy_aimd, .policy_ud=&aimd});
                } else {
                    run_aimd(c, a0, r, total);
                }
            }
        }

        printf("# COOLDOWN config=%s sleep=100\n", c->name);
        sleep(100);
    }

    printf("\n# sexp3_aimd ALL DONE\n");
    return 0;
}