/*
 * sexp1_mech.c —— 实验 1: α 机制验证 (α=0..100, step=1)
 *
 * 三组等tickets(100)、不同线程数(1/9/25)的纯spin任务,
 * 对每个α跑6000 tick,验证:
 *   1. α↓ → 重负载(t3) eff_tickets 被压
 *   2. CPU share 从 1:9:25 趋向 1:1:1
 *   3. Jain 从 ~0.5 趋向 ~1.0
 *
 * 输出: # mode=mech / W/ S/ K 行
 *
 * 用法:
 *   ./build/rmikuos run ./build/sexp1_mech.elf > ./logs/sched/mech/sexp1_mech.csv
 */
#include "schedlab.h"

#define TRIAL_TICKS  6000UL
#define WIN_TICKS    100

int main(void) {
    printf("# experiment=1_mech sweep=0..100 step=1 trial=%lu win=%d\n",
           (unsigned long)TRIAL_TICKS, WIN_TICKS);

    for (int a = 0; a <= 100; a++) {
        /* 重置 schedlab 全局 */
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

        /* 三组等tickets纯spin,不同线程数 */
        sl_add_spin("ctrl", 100,  1, 12000);
        sl_add_spin("log", 100,  9, 12000);
        sl_add_spin("ai", 100, 25, 12000);

        printf("# mode=mech alpha=%d threads=1,9,25 total=%lu\n",
               a, (unsigned long)TRIAL_TICKS);

        sl_run(&(sl_cfg){
            .total_ticks  = TRIAL_TICKS,
            .window_ticks = WIN_TICKS,
            .alpha0       = a,
            .policy       = 0,
            .policy_ud    = 0,
        });
    }

    printf("# done\n");
    return 0;
}