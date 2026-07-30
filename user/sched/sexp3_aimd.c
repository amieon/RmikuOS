/*
 * sexp3_aimd.c —— 实验 3: AIMD 恒定负载 + 多配置 + 多初始 α
 *
 * 4 个配置: light/medlo/medium/heavy
 * 每个配置:
 *   fixed 50 baseline × 3 reps
 *   AIMD α0=0  × 3 reps  (从安全区启动，验证不发散)
 *   AIMD α0=50 × 3 reps  (从膝点附近启动，验证稳态)
 *   AIMD α0=100 × 3 reps (从灾难区启动，验证收敛)
 * 总时长: 24000 ticks (~5.6 分钟/trial)
 *
 * ctrl 用 in-parent jobs（为了 D 行控制器反馈，AIMD 需要 late_delta）
 * ai/log 用 fork 子进程（不打断 ctrl）
 * wake_blocked_thread 只对 tickets≤1 重置 pass（exp2 修复），ctrl(tickets=300)
 * 不享受"免费优先"，α 真正通过 stride 竞争影响 miss。
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

static void setup(const cfg_t *c) {
    /* ctrl: in-parent jobs（D 行反馈），period=4, burn=180k≈1.2tick */
    sl_add_jobs_parent("ctrl", 180, 1, /*period*/4, /*cpu*/3, /*burn*/180000);
    sl_add_spin("ai",  100, c->ai,  12000);
    sl_add_spin("log",  50, c->log, 12000);
}

static void run_fixed(const cfg_t *c, int alpha, int rep, unsigned long total) {
    sl_reset_state();
    setup(c);
    printf("# RUN config=%s mode=fixed alpha=%d rep=%d/3\n", c->name, alpha, rep);
    sl_run(&(sl_cfg){
        .total_ticks = total, .window_ticks = 100,
        .alpha0 = alpha, .policy = 0, .policy_ud = 0,
    });
}

static void run_aimd(const cfg_t *c, int alpha0, int rep, unsigned long total) {
    sl_reset_state();
    setup(c);
    sl_aimd_t aimd;
    sl_aimd_init(&aimd, alpha0);
    /* 提高退避阈值：in-parent ctrl 被主线程每 100tick 打断 1tick，
     * late_delta 基线偏高。danger=25 太敏感，AIMD 一直退避到 0。
     * danger=100 让 AIMD 只在严重 miss 时退避，在 edge 附近 probe up。 */
    aimd.danger_lateness = 100;
    aimd.safe_lateness = 10;
    printf("# RUN config=%s mode=aimd alpha0=%d rep=%d/3\n", c->name, alpha0, rep);
    sl_run(&(sl_cfg){
        .total_ticks = total, .window_ticks = 100,
        .alpha0 = alpha0, .policy = sl_policy_aimd, .policy_ud = &aimd,
    });
}

int main(void) {
    const unsigned long total = 24000;
    const int nreps = 3;
    const int aimd_alphas[] = {0, 50, 100};

    printf("# sexp3_aimd: %d configs x (fixed50 + aimd0/50/100) x (1w+%dr), total=%lu\n",
           N_CFGS, nreps, total);
    printf("# ctrl=300tk,1t,p=4,burn=180k(in-parent)  ai=100tk,spin  log=50tk,spin\n");

    for (int ci = 0; ci < N_CFGS; ci++) {
        const cfg_t *c = &CFGS[ci];
        printf("\n# === CONFIG %s ai=%d log=%d ===\n", c->name, c->ai, c->log);

        /* fixed 50: warmup (不打 # RUN，被 Python skip) */
        printf("# WARMUP config=%s mode=fixed alpha=50\n", c->name);
        sl_reset_state(); setup(c);
        sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100,
                         .alpha0=50, .policy=0, .policy_ud=0});

        /* fixed 50: formal reps */
        for (int fa = 0; fa <= 100; fa += 50) {
            printf("# WARMUP config=%s mode=fixed alpha=%d\n", c->name, fa);
            sl_reset_state(); setup(c);
            sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100,
                             .alpha0=fa, .policy=0, .policy_ud=0});
            for (int r = 1; r <= nreps; r++) {
                run_fixed(c, fa, r, total);
            }
        }

        /* AIMD from 0, 50, 100 */
        for (int ai = 0; ai < 3; ai++) {
            int a0 = aimd_alphas[ai];

            /* warmup */
            printf("# WARMUP config=%s mode=aimd alpha0=%d\n", c->name, a0);
            sl_reset_state(); setup(c);
            sl_aimd_t aimd_w; sl_aimd_init(&aimd_w, a0);
            aimd_w.danger_lateness = 100;
            aimd_w.safe_lateness = 10;
            sl_run(&(sl_cfg){.total_ticks=total, .window_ticks=100,
                             .alpha0=a0, .policy=sl_policy_aimd, .policy_ud=&aimd_w});

            /* formal reps */
            for (int r = 1; r <= nreps; r++) {
                run_aimd(c, a0, r, total);
            }
        }
    }

    printf("\n# sexp3_aimd ALL DONE\n");
    return 0;
}
