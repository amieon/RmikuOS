/*
 * 81_schedlab_demo.c —— schedlab 版"动态负载实验"(v1,支持三种策略)
 *
 * 复刻并精简 40_dynamic_load_exp.c:
 *   ctrl: 周期 deadline 任务(交互式负载,高 tickets)
 *   ai:   全程 spin 怪(32 线程,压线程数维度)
 *   log:  轻量后台
 *   dyn:  中段加入的重载 spin(轻→重→轻三段)
 *
 * 用法(shell 内):
 *   schedlab_demo  [模式]  [alpha0]  [total_ticks]
 *   schedlab_demo                     = adamw 50 36000(默认)
 *   schedlab_demo fixed 75            = 固定 alpha=75(地面真值扫描用)
 *   schedlab_demo hill 50             = 迟滞爬山基线
 *   schedlab_demo adamw 50 36000      = SPSA-AdamW
 *
 * 输出 CSV(W/J/S 行)重定向后用宿主 Python 合并分析:
 *   ./run.sh loongarch64 debug < cmds.txt 2>&1 | tee logs/schedlab_raw.log
 *
 * 实验路线:
 *   1. fixed 扫 alpha ∈ {0,10,...,100} -> J(max_slowdown 均值) vs alpha 曲线
 *      = 地面真值,找到最优固定 alpha*;
 *   2. hill / adamw 各跑 3-5 次,对比收敛点、轨迹与 dyn 突变期的响应;
 *   3. 若 adamw 收敛点 ≠ alpha*,先怀疑 SPSA 梯度噪声,再怀疑地面真值。
 */
#include "schedlab.h"

static sl_adamw_t g_adamw;
static sl_hill_t  g_hill;

int main(int argc, char **argv) {
    const char   *mode  = argc > 1 ? argv[1] : "adamw";
    int          alpha0 = argc > 2 ? parse_int(argv[2]) : 50;
    unsigned long total = argc > 3 ? (unsigned long)parse_int(argv[3]) : 36000;

    sl_add_jobs("ctrl", 300, 8, /*period*/4, /*cpu*/2, /*burn*/400000);
    sl_add_spin("ai",   100, 32, 12000);
    sl_add_spin("log",   50,  4, 12000);
    sl_add_spin_phased("dyn", 100, 16, 12000,
                       /*start*/12000, /*run*/12000);

    sl_policy_t policy = sl_policy_adamw;
    void       *ud     = &g_adamw;

    if (str_eq(mode, "fixed")) {
        policy = 0;                     /* sl_run 内 alpha 冻结在 alpha0 */
        ud = 0;
    } else if (str_eq(mode, "hill")) {
        sl_hill_init(&g_hill, alpha0, /*step*/5);
        policy = sl_policy_hill;
        ud = &g_hill;
    } else {
        sl_adamw_init(&g_adamw, alpha0, /*lr*/8, /*target*/50);
    }

    printf("# mode=%s alpha0=%d total=%lu\n", mode, alpha0, total);

    sl_run(&(sl_cfg){
        .total_ticks  = total,
        .window_ticks = 100,
        .alpha0       = alpha0,
        .policy       = policy,
        .policy_ud    = ud,
    });
    return 0;
}