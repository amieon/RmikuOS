/*
 * schedlab.c —— 调度实验统一入口
 *
 * 五个实验,CLI 与原 tests/ 对齐:
 *   schedlab mech  <alpha> [t1 t2 t3] [total]         实验1 机制验证(默认 1 9 25, total 3600)
 *   schedlab edge  <alpha> [ctrl ai log] [total]      实验2 trade-off 固定α(默认 1 14 8, 3600)
 *   schedlab aimd  <alpha0> [ctrl ai log] [total]     实验3 AIMD 恒定负载(默认同 edge)
 *   schedlab dyn   <alpha0> [ctrl ai log] [total]     实验4 动态负载(默认 1 100 16, 36000)
 *   schedlab adamw <alpha0> [ctrl ai log] [total]     实验5 AdamW 对照(默认同 dyn)
 *
 * 任意位置出现 "fixed" → 强制固定 alpha(aimd/dyn/adamw 的 baseline)。
 * 例:schedlab dyn 50 1 100 16 fixed   ≡   原 dynamic_load_exp 50 1 100 16 fixed
 *
 * 负载参数与原 40_dynamic_load_exp.c 一致:
 *   ctrl: tickets 300, period 4, cpu 2, burn 400000(in-parent,控制器共享统计)
 *   ai:   tickets 100, burn 12000;dyn 模式为三段相位(轻相位 3 线程活跃)
 *   log:  tickets 50, burn 12000
 */
#include "schedlab.h"

static sl_aimd_t  g_aimd;
static sl_adamw_t g_adamw;

static int has_fixed_flag(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "fixed")) return 1;
    }
    return 0;
}

static int arg_or(int argc, char **argv, int i, int dflt) {
    if (i < argc && !str_eq(argv[i], "fixed")) return parse_int(argv[i]);
    return dflt;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: schedlab <mech|edge|aimd|dyn|adamw> <alpha0> "
               "[t1 t2 t3 | ctrl ai log] [total] [fixed]\n");
        return 1;
    }

    const char *mode  = argv[1];
    int        alpha0 = parse_int(argv[2]);
    int        fixed  = has_fixed_flag(argc, argv);

    sl_policy_t policy = 0;
    void       *ud     = 0;

    if (str_eq(mode, "mech")) {
        /* 实验1:三组同 tickets 不同线程数,固定 α,看 eff_tickets 与 tick share */
        int t1 = arg_or(argc, argv, 3, 1);
        int t2 = arg_or(argc, argv, 4, 9);
        int t3 = arg_or(argc, argv, 5, 25);
        unsigned long total = (unsigned long)arg_or(argc, argv, 6, 3600);
        sl_add_spin("t1", 100, t1, 12000);
        sl_add_spin("t2", 100, t2, 12000);
        sl_add_spin("t3", 100, t3, 12000);
        printf("# mode=mech alpha=%d threads=%d,%d,%d total=%lu\n",
               alpha0, t1, t2, t3, total);
        sl_run(&(sl_cfg){ .total_ticks = total, .window_ticks = 100,
                          .alpha0 = alpha0, .policy = 0, .policy_ud = 0 });
        return 0;
    }

    /* 实验 2-5 共用负载参数:ctrl ai log */
    int ctrl_t = arg_or(argc, argv, 3, 1);
    int ai_t, log_t;
    unsigned long total;
    int phased = 0;

    if (str_eq(mode, "edge") || str_eq(mode, "aimd")) {
        ai_t  = arg_or(argc, argv, 4, 14);
        log_t = arg_or(argc, argv, 5, 8);
        total = (unsigned long)arg_or(argc, argv, 6, 3600);
    } else if (str_eq(mode, "dyn") || str_eq(mode, "adamw")) {
        ai_t  = arg_or(argc, argv, 4, 100);
        log_t = arg_or(argc, argv, 5, 16);
        total = (unsigned long)arg_or(argc, argv, 6, 36000);
        phased = 1;
    } else {
        printf("unknown mode: %s\n", mode);
        return 1;
    }

    sl_add_jobs_parent("ctrl", 300, ctrl_t, /*period*/4, /*cpu*/2, /*burn*/400000);
    if (phased) sl_add_spin_phased("ai", 100, ai_t, 12000, /*light_active*/3);
    else        sl_add_spin("ai", 100, ai_t, 12000);
    sl_add_spin("log", 50, log_t, 12000);

    if (!fixed) {
        if (str_eq(mode, "aimd") || str_eq(mode, "dyn")) {
            sl_aimd_init(&g_aimd, alpha0);
            policy = sl_policy_aimd;
            ud = &g_aimd;
        } else if (str_eq(mode, "adamw")) {
            sl_adamw_init(&g_adamw, alpha0, /*lr*/8, /*target*/50);
            policy = sl_policy_adamw;
            ud = &g_adamw;
        }
    }

    printf("# mode=%s alpha0=%d ctrl=%d ai=%d log=%d total=%lu %s\n",
           mode, alpha0, ctrl_t, ai_t, log_t, total,
           fixed ? "fixed" : "adaptive");

    sl_run(&(sl_cfg){
        .total_ticks  = total,
        .window_ticks = 100,
        .alpha0       = alpha0,
        .policy       = policy,
        .policy_ud    = ud,
    });
    return 0;
}