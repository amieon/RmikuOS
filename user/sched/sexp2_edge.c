/*
 * sexp2_edge.c — Experiment 2: Edge deadline trade-off (alpha sweep)
 *
 * 遍历 alpha ∈ {1,5,10,15,20,30,40,50,60,70,80,90,100},
 * 每个 alpha 跑 edge 负载(ctrl/ai/log) 3600 ticks,固定 alpha 无自适应。
 * 输出 E 行汇总 + 完整 W/D/S/J/K 轨迹,供宿主机解析画 trade-off 曲线。
 *
 * 输出格式:
 *   # --- alpha=<a> ---                    (分隔标记)
 *   W,<win>,<alpha>,<pid>,<name>,...       (每窗口)
 *   D,<win>,<alpha>,<jobs_d>,<miss_d>,<late_d>
 *   S,<win>,<alpha>,<jain_q>,<max_slowdown_q>
 *   J,<pid>,ctrl,1,<jobs>,<miss>,...       (ctrl 汇总)
 *   K,<pid>,ai,15,<work>                   (ai 汇总)
 *   K,<pid>,log,9,<work>                   (log 汇总)
 *   E,<alpha>,<jobs>,<miss>,<late_sum>,<late_max>,<resp_sum>,<resp_sumsq>,<resp_min>,<resp_max>
 *
 * 注意:sl_run 内 prev_run[] 是 static local,第二次调用时 window 1 的
 * run_delta 会因 reset_sched_stat 后计数器归零而下溢。仅影响 W 行第 1 窗口,
 * J/K/E 行不受影响(trade-off 曲线用 E+K 即可)。
 */
#include "schedlab.h"

#define N_ALPHAS 13

static int sweep_alphas[N_ALPHAS];   /* BSS: main 里赋值 */

static void zero_all_state(void) {
    sl_ngroups = 0;
    sl_window  = 0;
    sl_t0      = 0;
    sl_t_end   = 0;
    for (int i = 0; i < SL_MAX_GROUPS; i++) {
        char *p = (char *)&sl_groups[i];
        for (unsigned j = 0; j < sizeof(sl_group_t); j++) p[j] = 0;
        char *q = (char *)&sl_gstats[i];
        for (unsigned j = 0; j < sizeof(sl_gstats_t); j++) q[j] = 0;
    }
    /* sl_args 不需要清零,sl_run 会重新填充 */
}

int main(void) {
    /* BSS 规则:所有初值在 main 里赋 */
    sweep_alphas[0]  = 1;
    sweep_alphas[1]  = 5;
    sweep_alphas[2]  = 10;
    sweep_alphas[3]  = 15;
    sweep_alphas[4]  = 20;
    sweep_alphas[5]  = 30;
    sweep_alphas[6]  = 40;
    sweep_alphas[7]  = 50;
    sweep_alphas[8]  = 60;
    sweep_alphas[9]  = 70;
    sweep_alphas[10] = 80;
    sweep_alphas[11] = 90;
    sweep_alphas[12] = 100;

    printf("# sexp2_edge: fixed-alpha trade-off sweep\n");
    printf("# ctrl: tickets=300 threads=1 period=4 cpu=2 burn=400000 (in-parent)\n");
    printf("# ai:   tickets=100 threads=14 burn=12000 (spin)\n");
    printf("# log:  tickets=50  threads=8  burn=12000 (spin)\n");
    printf("# per-point: total=3600 win=100 start_delay=80\n");
    printf("# E,<alpha>,<jobs>,<miss>,<late_sum>,<late_max>,<resp_sum>,<resp_sumsq>,<resp_min>,<resp_max>\n");

    for (int ai = 0; ai < N_ALPHAS; ai++) {
        int alpha = sweep_alphas[ai];

        zero_all_state();

        sl_add_jobs_parent("ctrl", 300, 1, /*period*/4, /*cpu*/2, /*burn*/400000);
        sl_add_spin("ai",  100, 14, 12000);
        sl_add_spin("log",  50,  8, 12000);

        printf("# --- alpha=%d ---\n", alpha);

        sl_run(&(sl_cfg){
            .total_ticks  = 3600,
            .window_ticks = 100,
            .alpha0       = alpha,
            .start_delay  = 80,
            .policy       = 0,
            .policy_ud    = 0,
        });

        /* E 行:ctrl 汇总(group 0, in-parent, sl_gstats 已归零重计) */
        sl_gstats_t *cs = &sl_gstats[0];
        unsigned long rmin = cs->resp_min == (unsigned long)-1 ? 0 : cs->resp_min;
        printf("E,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
               alpha,
               cs->jobs, cs->miss, cs->late_sum, cs->late_max,
               cs->resp_sum, cs->resp_sumsq, rmin, cs->resp_max);
    }

    printf("# done\n");
    return 0;
}