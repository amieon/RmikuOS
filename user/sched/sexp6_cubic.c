/*
 * sexp6_cubic.c —— 实验 6: CUBIC(TCP 拥塞控制移植, RFC 8312 思路)
 *
 * 编号说明: exp6 v1 是 SPSA-AdamW(sexp6_adamw.c, 数据在
 *   logs/sched/adamw/)。AdamW 家族全部归入新 exp7 的优化器 ablation,
 *   exp6 编号让位给 CUBIC —— 叙事: AIMD 即 Reno 血统(线性 +5/窗),
 *   CUBIC 是 Linux 默认拥塞控制的三次曲线版本, 补全"网络协议演化"线。
 *
 * 负载配置同 exp4/5(一个字不改): ctrl=300tk,1t,p=5,burn=180k(in-parent)
 *   ai=100tk,50t,phased(light_active=5)  log=50tk,3t,spin
 *
 * 3 种相位比例(修正 v1 的 ratio 误读):
 *   ratio=500: 25/25/25/25 (真等分; 与 exp4 的 ratio=0 负载等价,
 *              AIMD/fixed 基线直接复用 exp4 CSV)
 *   ratio=800: 40/10/40/10 (L占80%)
 *   ratio=200: 10/40/10/40 (H占80%)
 *
 * modes: cubic0/50/100 —— β=0.7, C=0.06(fp61), danger=25(同 AIMD 判据)
 *
 * 诚实假设(先验, 可证伪): α 值域仅 0-100(远小于 TCP cwnd 数千),
 *   CUBIC 收复速度与 AIMD 相近; 可测差异是"逼近危险区减速缓行"
 *   → 过冲更少 → A 行 down 次数显著少于 AIMD(统计脚本数 down)。
 *
 * 3 ratios × 3 modes × (1 warmup + 3 reps) = 36 runs × 56min ≈ 34h
 *
 * warmup 与正式 reps 参数完全一致(吸取 exp3 的教训, 无隐藏差异)。
 *
 * 用法:
 *   ./sched/sexp6_cubic > /tmp/sexp6_cubic.csv
 *   (行格式 W/D/A/S/J/K 与 stat_exp6.py 的解析约定一致, mode 名新增)
 */
#include "schedlab.h"

static void sl_reset_state(void) {
    sl_ngroups = 0;
    for (int i = 0; i < SL_MAX_GROUPS; i++) {
        sl_gstats[i].work = 0; sl_gstats[i].jobs = 0; sl_gstats[i].miss = 0;
        sl_gstats[i].late_sum = 0; sl_gstats[i].late_max = 0;
        sl_gstats[i].resp_sum = 0; sl_gstats[i].resp_sumsq = 0;
        sl_gstats[i].resp_min = 0; sl_gstats[i].resp_max = 0;
    }
}

static void setup(void) {
    sl_add_jobs_parent("ctrl", 300, 1, /*period*/5, /*cpu*/3, /*burn*/180000);
    sl_add_spin_phased("ai", 100, 50, 12000, /*light_active*/5);
    sl_add_spin("log", 50, 3, 12000);
}

/* 控制器实例 .bss 静态分配(框架头文件的 .bss 教训, 不放栈上) */
static sl_cubic_t g_cubic;

static void run_cubic(int l_ratio, const char *mode, int alpha0,
                      int rep /* 0=warmup */, unsigned long total) {
    sl_reset_state();
    setup();
    sl_l_ratio_permil = l_ratio;

    sl_cubic_init(&g_cubic, alpha0);

    if (rep == 0)
        printf("# WARMUP ratio=%d mode=%s alpha0=%d\n", l_ratio, mode, alpha0);
    else
        printf("# RUN ratio=%d mode=%s alpha0=%d rep=%d/3\n",
               l_ratio, mode, alpha0, rep);

    sl_run(&(sl_cfg){
        .total_ticks = total,
        .window_ticks = 100,
        .alpha0 = alpha0,
        .policy = sl_policy_cubic,
        .policy_ud = &g_cubic,
    });

    sl_l_ratio_permil = 0;
}

int main(void) {
    const unsigned long total = 240000;
    const int nreps = 3;

    struct { int ratio; const char *name; } ratios[] = {
        {500, "25/25"},
        {800, "40/10"},
        {200, "10/40"},
    };
    const int nratios = 3;

    struct { const char *mode; int alpha0; } modes[] = {
        {"cubic0",     0},
        {"cubic50",   50},
        {"cubic100", 100},
    };
    const int nmodes = 3;

    printf("# sexp6_cubic: CUBIC controller, %d ratios x %d modes x %d reps, total=%lu\n",
           nratios, nmodes, nreps, total);
    printf("# ctrl=300tk,1t,p=5,burn=180k(in-parent)\n");
    printf("# ai=100tk,50t,phased(light_active=5)  log=50tk,3t,spin\n");
    printf("# cubic: beta=70%%, C=0.06(fp61), danger=25(同 AIMD 判据)\n");
    printf("# 对照: AIMD 数据复用 exp4(ratio=0=500)/exp5(800/200), stat 合并\n");

    for (int ri = 0; ri < nratios; ri++) {
        printf("\n# === RATIO %s (permil=%d) ===\n", ratios[ri].name, ratios[ri].ratio);

        for (int mi = 0; mi < nmodes; mi++) {
            run_cubic(ratios[ri].ratio, modes[mi].mode, modes[mi].alpha0,
                      /*warmup*/0, total);
            for (int r = 1; r <= nreps; r++) {
                run_cubic(ratios[ri].ratio, modes[mi].mode, modes[mi].alpha0,
                          r, total);
            }
        }
    }

    printf("\n# sexp6_cubic ALL DONE\n");
    return 0;
}
