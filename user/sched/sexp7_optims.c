/*
 * sexp7_optims.c —— 实验 7: 深度学习优化器家族 ablation
 *                   (SGD-M / RMSProp / AdaGrad / AdamW)
 *
 * 编号说明: 吸收原 exp6 v1(sexp6_adamw.c)的 AdamW, 四兄弟同批重跑:
 *   ablation 必须同批(批间环境漂移会污染组件贡献的读数), 旧 CSV 只做
 *   交叉验证用。四者共用同一 SPSA 梯度估计 + 同一 loss + 同 lr=3,
 *   只换更新公式:
 *     sgdm   : 只有动量(m=0.9m+0.1g), 无自适应, 无 decay
 *     rmsprop: 只有自适应(v=0.99v+0.01g²), 无动量, 无 decay
 *     adagrad: 自适应但 v 不衰减累积 → 步长单调萎缩, 无 decay
 *     adamw  : 动量+自适应+weight decay(target=25) —— 完整版
 *   adamw − 三兄弟的差 = 各组件/decay 的净贡献。
 *   量纲: SGD-M 用 G_REF=10240 归一(典型|g|), 四者稳态步长均 ~3 点/窗。
 *
 * 负载配置同 exp4/5/6(一个字不改), 3 ratios(500/800/200, 同 exp6)。
 * 起点: 统一 α0=50 —— ablation 比较的是更新公式, 不是起点鲁棒性
 *   (AdamW 三起点鲁棒性 exp6 v1 已测, 有 CSV)。
 *
 * 先验(可证伪):
 *   - 调度信号(相邻窗 loss 差分)噪声大 → sgdm 抖(A 行 up/down 频繁)
 *   - adagrad 在 2400 窗的长 run 里步长萎缩 → 后期 α 呆滞
 *   - rmsprop 接近 adamw(差一个动量+decay), 若差距大则动量关键
 *   - adamw 靠 decay 爬升(exp6 v1 的结论), 三兄弟无 decay →
 *     loss=0 时全部梯度归零 → 爬升能力是 decay 送的(直接验证)
 *
 * 3 ratios × 4 modes × (1 warmup + 3 reps) = 48 runs × 56min ≈ 45h
 *
 * 用法:
 *   ./sched/sexp7_optims > /tmp/sexp7_optims.csv
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

/* 控制器实例 .bss 静态分配 */
static sl_optim_t g_optim;
static sl_adamw_t g_adamw;

enum { K_SGDM, K_RMSPROP, K_ADAGRAD, K_ADAMW };

static void run_mode(int kind, const char *mode, int l_ratio,
                     int rep /* 0=warmup */, unsigned long total) {
    sl_reset_state();
    setup();
    sl_l_ratio_permil = l_ratio;

    sl_cfg cfg = {
        .total_ticks = total,
        .window_ticks = 100,
        .alpha0 = 50,
        .policy = 0,
        .policy_ud = 0,
    };
    switch (kind) {
    case K_ADAMW:
        /* 与 exp6 v1 完全同参: lr=3, target=25 */
        sl_adamw_init(&g_adamw, /*alpha0*/50, /*lr*/3, /*target*/25);
        cfg.policy = sl_policy_adamw; cfg.policy_ud = &g_adamw;
        break;
    default:
        /* 三兄弟同 lr=3, 无 decay(结构性没有, 不是参数差异) */
        sl_optim_init(&g_optim, kind, /*alpha0*/50, /*lr*/3);
        cfg.policy = sl_policy_optim; cfg.policy_ud = &g_optim;
        break;
    }

    if (rep == 0)
        printf("# WARMUP ratio=%d mode=%s alpha0=50\n", l_ratio, mode);
    else
        printf("# RUN ratio=%d mode=%s alpha0=50 rep=%d/3\n",
               l_ratio, mode, rep);

    sl_run(&cfg);

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

    struct { const char *mode; int kind; } modes[] = {
        {"sgdm",    K_SGDM},
        {"rmsprop", K_RMSPROP},
        {"adagrad", K_ADAGRAD},
        {"adamw",   K_ADAMW},
    };
    const int nmodes = 4;

    printf("# sexp7_optims: optimizer family ablation, %d ratios x %d modes x %d reps, total=%lu\n",
           nratios, nmodes, nreps, total);
    printf("# ctrl=300tk,1t,p=5,burn=180k(in-parent)\n");
    printf("# ai=100tk,50t,phased(light_active=5)  log=50tk,3t,spin\n");
    printf("# 全家: SPSA delta=5, lr=3, alpha0=50 | adamw target=25\n");
    printf("# adamw 同批重跑作锚点(旧 exp6 v1 CSV 交叉验证)\n");

    for (int ri = 0; ri < nratios; ri++) {
        printf("\n# === RATIO %s (permil=%d) ===\n", ratios[ri].name, ratios[ri].ratio);

        for (int mi = 0; mi < nmodes; mi++) {
            run_mode(modes[mi].kind, modes[mi].mode,
                     ratios[ri].ratio, /*warmup*/0, total);
            for (int r = 1; r <= nreps; r++) {
                run_mode(modes[mi].kind, modes[mi].mode,
                         ratios[ri].ratio, r, total);
            }
        }
    }

    printf("\n# sexp7_optims ALL DONE\n");
    return 0;
}
