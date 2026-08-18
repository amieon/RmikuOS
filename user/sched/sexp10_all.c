/*
 * sexp10_all.c —— 实验 10: 统一矩阵同批重跑(全部控制器同一批次)
 *
 * 为什么需要: exp0-9 各自分批跑, ai_burn(K 行)依赖主机速度、ai_run 有
 *   跨批次调度噪声, 方法间对比被环境漂移污染(见 sexp7_optims 的排查:
 *   旧 adamw ~1M vs 新 ~800k, 20% 是漂移不是差异)。统一矩阵把所有
 *   控制器放在同一批、同一主机、同一 QEMU 参数下跑, 让"方法 vs 方法"
 *   的对比在噪声内可信。
 *
 * 模式矩阵(21 种, 与旧实验命名一致, stat 脚本可直接复用):
 *   fixed 0/25/50/75/100        5  固定基线(无控制器)
 *   aimd  0/50/100              3  启发式(三起点)
 *   cubic 0/50/100              3  网络协议(三起点)
 *   sgdm/rmsprop/adagrad        3  优化器 ablation(单起点=50, 比更新公式)
 *   adamw 0/50/100              3  优化器完整版(三起点, 鲁棒性)
 *   pid   0/50/100              3  经典控制(三起点; pid50 为 exp8 未测的新增)
 *   ucb   50                    1  在线学习(单起点; 冷启动逐臂扫, 起点结构上无意义)
 *                         合计 21 模式
 *
 * 负载配置同 exp4-9(一个字不改): ctrl=300tk,1t,p=5,burn=180k(in-parent)
 *   ai=100tk,50t,phased(light_active=5)  log=50tk,3t,spin
 * 3 ratios(500/800/200)。
 *
 * 控制器参数(全部沿用各实验标定, 一个不改):
 *   aimd : inc=5, backoff=80%, safe=0, danger=25, down 后 cooldown=3
 *   cubic: beta=70%, C=0.06(fp61), danger=25
 *   optim/adamw: SPSA delta=5, lr=3, G_REF=10240; adamw target=25
 *   pid  : kp=150, ki=64(fp1024), target=10
 *   ucb  : 11 arms, tau=32, c_explore=1000, reward=10000-loss
 *
 * 总时长: 21 modes × 3 ratios × (1 warmup + 3 reps) = 252 runs × 56min
 *   ≈ 235h ≈ 10 天。warmup 占 63 runs ≈ 2.5 天(可去掉省时, 见 run 段注释)。
 *
 * 用法:
 *   ./sched/sexp10_all > /tmp/sexp10_all.csv
 *   (行格式 W/D/A/S/J/K 与 stat 脚本解析约定一致, mode 名见上矩阵)
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

/* 控制器实例 .bss 静态分配(框架头文件的 .bss 教训: 放栈上有爆栈风险,
 * 尤其 sl_ucb_t ~3.2KB) */
static sl_aimd_t  g_aimd;
static sl_cubic_t g_cubic;
static sl_optim_t g_optim;
static sl_adamw_t g_adamw;
static sl_pid_t   g_pid;
static sl_ucb_t   g_ucb;

/* 控制器种类(与 SL_OPTIM_* 无关的独立编号, optim 三兄弟在 switch 里映射) */
enum {
    K_FIXED = 0,
    K_AIMD,
    K_CUBIC,
    K_SGDM,
    K_RMSPROP,
    K_ADAGRAD,
    K_ADAMW,
    K_PID,
    K_UCB,
};

static void run_one(const char *mode, int kind, int alpha0, int l_ratio,
                    int rep /* 0=warmup */, unsigned long total) {
    sl_reset_state();
    setup();
    sl_l_ratio_permil = l_ratio;

    sl_cfg cfg = {
        .total_ticks = total,
        .window_ticks = 100,
        .alpha0 = alpha0,
        .policy = 0,
        .policy_ud = 0,
    };

    switch (kind) {
    case K_FIXED:
        /* policy=NULL → sl_run 只在开头 set_sched_alpha(alpha0) 一次, 之后不变 */
        break;
    case K_AIMD:
        sl_aimd_init(&g_aimd, alpha0);
        cfg.policy = sl_policy_aimd; cfg.policy_ud = &g_aimd;
        break;
    case K_CUBIC:
        sl_cubic_init(&g_cubic, alpha0);
        cfg.policy = sl_policy_cubic; cfg.policy_ud = &g_cubic;
        break;
    case K_SGDM:
        sl_optim_init(&g_optim, SL_OPTIM_SGDM, alpha0, /*lr*/3);
        cfg.policy = sl_policy_optim; cfg.policy_ud = &g_optim;
        break;
    case K_RMSPROP:
        sl_optim_init(&g_optim, SL_OPTIM_RMSPROP, alpha0, /*lr*/3);
        cfg.policy = sl_policy_optim; cfg.policy_ud = &g_optim;
        break;
    case K_ADAGRAD:
        sl_optim_init(&g_optim, SL_OPTIM_ADAGRAD, alpha0, /*lr*/3);
        cfg.policy = sl_policy_optim; cfg.policy_ud = &g_optim;
        break;
    case K_ADAMW:
        /* 与 exp6 v1 / exp7 完全同参: lr=3, target=25 */
        sl_adamw_init(&g_adamw, alpha0, /*lr*/3, /*target*/25);
        cfg.policy = sl_policy_adamw; cfg.policy_ud = &g_adamw;
        break;
    case K_PID:
        /* kp=150, ki=64(fp1024), target=10(同 exp8) */
        sl_pid_init(&g_pid, alpha0, /*kp_fp*/150, /*ki_fp*/64, /*target*/10);
        cfg.policy = sl_policy_pid; cfg.policy_ud = &g_pid;
        break;
    case K_UCB:
        sl_ucb_init(&g_ucb, alpha0);
        cfg.policy = sl_policy_ucb; cfg.policy_ud = &g_ucb;
        break;
    }

    if (rep == 0)
        printf("# WARMUP ratio=%d mode=%s alpha0=%d\n", l_ratio, mode, alpha0);
    else
        printf("# RUN ratio=%d mode=%s alpha0=%d rep=%d/3\n",
               l_ratio, mode, alpha0, rep);

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

    struct { const char *name; int kind; int alpha0; } modes[] = {
        {"fixed0",   K_FIXED,     0},
        {"fixed25",  K_FIXED,    25},
        {"fixed50",  K_FIXED,    50},
        {"fixed75",  K_FIXED,    75},
        {"fixed100", K_FIXED,   100},
        {"aimd0",    K_AIMD,      0},
        {"aimd50",   K_AIMD,     50},
        {"aimd100",  K_AIMD,    100},
        {"cubic0",   K_CUBIC,     0},
        {"cubic50",  K_CUBIC,    50},
        {"cubic100", K_CUBIC,   100},
        {"sgdm",     K_SGDM,     50},
        {"rmsprop",  K_RMSPROP,  50},
        {"adagrad",  K_ADAGRAD,  50},
        {"adamw0",   K_ADAMW,     0},
        {"adamw50",  K_ADAMW,    50},
        {"adamw100", K_ADAMW,   100},
        {"pid0",     K_PID,       0},
        {"pid50",    K_PID,      50},
        {"pid100",   K_PID,     100},
        {"ucb",      K_UCB,      50},
    };
    const int nmodes = sizeof(modes) / sizeof(modes[0]);

    printf("# sexp10_all: unified matrix, %d ratios x %d modes x %d reps, total=%lu\n",
           nratios, nmodes, nreps, total);
    printf("# ctrl=300tk,1t,p=5,burn=180k(in-parent)\n");
    printf("# ai=100tk,50t,phased(light_active=5)  log=50tk,3t,spin\n");
    printf("# params: aimd(inc5/backoff80/danger25) cubic(beta70/C0.06/danger25)\n");
    printf("#   optim/adamw(SPSA delta5 lr3, adamw target25) pid(kp150 ki64 target10)\n");
    printf("#   ucb(11arms tau32 c1000) fixed(policy=NULL)\n");
    printf("# modes=%d, 预计 %lu runs x 56min ≈ %lu 小时\n",
           nmodes,
           (unsigned long)nmodes * nratios * (1 + nreps),
           (unsigned long)nmodes * nratios * (1 + nreps) * 56 / 60);

    for (int ri = 0; ri < nratios; ri++) {
        printf("\n# === RATIO %s (permil=%d) ===\n", ratios[ri].name, ratios[ri].ratio);

        for (int mi = 0; mi < nmodes; mi++) {
            /* warmup(rep=0) 与正式 reps 参数完全一致(吸取 exp3 教训)。
             * 注意: warmup 数据被 stat 脚本丢弃(parse_csv 遇 # WARMUP 关段),
             * 只起预热 QEMU/主机状态的作用。想省时可在下面去掉 warmup 调用。 */
            run_one(modes[mi].name, modes[mi].kind, modes[mi].alpha0,
                    ratios[ri].ratio, /*warmup*/0, total);
            for (int r = 1; r <= nreps; r++) {
                run_one(modes[mi].name, modes[mi].kind, modes[mi].alpha0,
                        ratios[ri].ratio, r, total);
            }
        }
    }

    printf("\n# sexp10_all ALL DONE\n");
    return 0;
}
