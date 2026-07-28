/*
 * sexp1_mech.c —— 实验 1: α 机制验证
 *
 * 三组等 tickets(100)、不同线程数(1/9/25)的纯 spin 任务，
 * α=0..100 扫描，验证:
 *   1. eff_tickets = tickets × scale(threads, α) 单调不降
 *   2. CPU share ∝ eff_tickets
 *   3. α=0 → 三组公平(1:1:1)，α=100 → 按线程数(1:9:25)
 *
 * 用法:
 *   ./sched/sexp1_mech > ./logs/sched/mech/sexp1_mech.csv
 *   python3 ./scripts/sched/stat_exp1.py ./logs/sched/mech/sexp1_mech.csv
 *
 * 注意: 101 个 α 点 × 6000 tick ≈ 3 小时（1 tick=17.51ms）。
 *       调试时可用 ALPHA_STEP=5（21 个点，~37 分钟）。
 */
#include "schedlab.h"

#define TRIAL_TICKS  6000UL
#define WIN_TICKS    100
#define ALPHA_STEP   1

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

int main(void) {
    set_my_tickets(1);  /* 排除 schedlab 自身干扰 */

    printf("# sexp1_mech: alpha mechanism verification\n");
    printf("# sweep=0..100 step=%d trial=%lu win=%d\n",
           ALPHA_STEP, (unsigned long)TRIAL_TICKS, WIN_TICKS);
    printf("# t1=tickets=100,1thread  t2=tickets=100,9threads  t3=tickets=100,25threads\n");

    for (int a = 0; a <= 100; a += ALPHA_STEP) {
        sl_reset_state();

        sl_add_spin("t1", 100, 1, 12000);
        sl_add_spin("t2", 100, 9, 12000);
        sl_add_spin("t3", 100, 25, 12000);

        printf("# TRIAL alpha=%d threads=1,9,25 total=%lu\n",
               a, (unsigned long)TRIAL_TICKS);

        sl_run(&(sl_cfg){
            .total_ticks  = TRIAL_TICKS,
            .window_ticks = WIN_TICKS,
            .alpha0       = a,
            .policy       = 0,
            .policy_ud    = 0,
        });
    }

    printf("# sexp1_mech ALL DONE\n");
    return 0;
}
