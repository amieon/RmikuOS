/*
 * schedlab.h —— RmikuOS 调度实验框架
 *
 * 实验体系(exp0-9, 十个实验; 驱动文件见 user/sched/sexp*.c):
 *   0. edf    EDF 基线             —— 证明"CPU 公平 ≠ deadline 保障"
 *   1. mech   机制验证             —— 等 tickets 不同线程数 × α 全扫描, 证 eff=n^(α/100)
 *   2. edge   权衡刻画             —— 5 压力档 × 11 α, deadline vs 吞吐 trade-off
 *   3. aimd   AIMD 自适应          —— 启发式基线, 恒定负载(原 37/39/40)
 *   4. dyn    动态负载             —— L/H 相位交替, 逼控制器跟风
 *   5. phase  相位比例             —— L 占比对 AIMD 优势的边际曲线
 *   6. cubic  CUBIC 迁移           —— 网络协议流派(旧 sexp6_adamw.c 的 adamw 已并入 exp7)
 *   7. optims 优化器家族 ablation  —— SGD-M / RMSProp / AdaGrad / AdamW, 深度优化流派
 *   8. pid    增量式 PI            —— 经典控制流派(对照组)
 *   9. ucb    滑窗 UCB             —— 在线学习流派(离散决策, Garivier & Moulines 2011)
 *
 * 五方法流派同台(核心叙事): 启发式(AIMD) / 网络协议(CUBIC) / 深度优化器
 * (AdamW + 三 ablation) / 经典控制(PI) / 在线学习(UCB)。
 * 策略层全部是 sl_policy_t 回调(sl_policy_aimd/_cubic/_adamw/_optim/_pid/_ucb),
 * 共用 sl_deadline_loss 信号源与 A 行轨迹, 插拔式对比, 不碰内核调度。
 *
 *
 * 内核依赖(现状已满足,无需新增 syscall):
 *   fork/thread_create/thread_exit/sleep/get_ticks/getpid/exit/waitpid
 *   set_my_tickets/set_sched_alpha/get_process_sched_stat/reset_sched_stat
 *
 * 输出 CSV(全部原始量,推导交给宿主机 Python):
 *   W,win,alpha,pid,name,run_delta,eff_tickets,ready_threads
 *   D,win,alpha,jobs_delta,miss_delta,late_delta      (仅 in-parent jobs 组)
 *   A,win,alpha_before,alpha_after,action             (控制器决策轨迹, 格式按策略分三种)
 *     - AIMD / CUBIC / PI: 4 列, action = up/down/hold
 *     - AdamW / 优化器家族(SGD-M/RMSProp/AdaGrad): 8 列
 *         A,win,before,after,action,loss,g,step,decay
 *         loss/g/step/decay 均为 ×1024 定点原值; 三兄弟无 weight decay, decay≡0
 *         (供定理3 分量分解: 安全区 loss≈0 时 step→0、decay 是唯一漂移源)
 *     - UCB: 4 列, action = armN(第5列臂号, 选中臂的 α = N×10)
 *   J,pid,name,threads,jobs,miss,late_sum,late_max,resp_sum,resp_sumsq,resp_min,resp_max
 *   K,pid,name,threads,work                           (spin 组收尾:吞吐 work)
 *   S,win,next_alpha,jain_q,max_slowdown_q
 *
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "user.h"

/* #define printf uprintf */

#define SL_MAX_GROUPS  8
#define SL_MAX_THREADS 512
#define SL_NAME_LEN    16

/* ================= 数据类型 ================= */

typedef enum { SL_SPIN = 0, SL_JOBS = 1 } sl_kind_t;

#define SL_F_IN_PARENT  1   /* jobs 组跑在监控进程内(共享统计,控制器可读) */
#define SL_F_PHASED     2   /* spin 组三段相位:中段全活跃,轻段只 light_active 个活跃 */

typedef struct {
    char     name[SL_NAME_LEN];
    int      tickets;
    int      threads;
    sl_kind_t kind;
    int      flags;
    int      light_active;   /* SL_F_PHASED:轻相位活跃线程数(原 DYN_LIGHT_ACTIVE=3) */
    int      period_ticks;   /* SL_JOBS:释放周期 */
    int      job_cpu_ticks;  /* 记账用 */
    unsigned long burn;
    int      pid;            /* 运行时填充;in-parent 组 = getpid() */
} sl_group_t;

/* 每组统计(进程内共享,AMO 更新;min/max 允许竞态仅作参考) */
typedef struct {
    unsigned long work;        /* spin:burn 迭代总数(吞吐) */
    unsigned long jobs, miss, late_sum, late_max;
    unsigned long resp_sum, resp_sumsq, resp_min, resp_max;
} sl_gstats_t;

typedef struct {
    int  pid;
    char name[SL_NAME_LEN];
    int  eff_tickets;
    int  ready_threads;
    unsigned long run_delta;
    int  share_q;
    int  entitled_q;
    int  slowdown_q;
} sl_proc_t;

typedef struct {
    int window_no;
    int alpha;
    int remain_windows;      /* late-probe 保护用 */
    int nprocs;
    sl_proc_t procs[SL_MAX_GROUPS];
    int jain_q;
    int max_slowdown_q;
    /* 窗口 deadline 差分(仅 in-parent jobs 组;否则为 0) */
    unsigned long jobs_delta, miss_delta, late_delta;
} sl_window_t;

typedef int (*sl_policy_t)(const sl_window_t *w, void *ud);

typedef struct {
    unsigned long total_ticks;
    int           window_ticks;
    int           alpha0;
    unsigned long start_delay;    /* 0 = 用默认 80 */
    sl_policy_t   policy;         /* NULL = 固定 alpha0 */
    void         *policy_ud;
} sl_cfg;

/* ================= 全局状态(全部零初始化,.bss!见文件头警告) ================= */

static sl_group_t  sl_groups[SL_MAX_GROUPS];
static sl_gstats_t sl_gstats[SL_MAX_GROUPS];
static int         sl_ngroups;
static unsigned long sl_t0, sl_t_end;
static int         sl_window;
/* 相位比例: L段占每个L-H周期的比例(千分比),0=等分(25/25/25/25)。
 * 例: 800=40/10/40/10(L占80%), 200=10/40/10/40(H占80%)。
 * 每个 L-H 周期各占 span/2,L段 = half*ratio/1000,H段 = half*(1-ratio/1000)。 */
static int         sl_l_ratio_permil = 0;

/* ================= 负载注册 ================= */

static int sl_add_group(const char *name, int tickets, int threads,
                        sl_kind_t kind, int flags, int light_active,
                        int period, int job_cpu, unsigned long burn)
{
    if (sl_ngroups >= SL_MAX_GROUPS) return -1;
    sl_group_t *g = &sl_groups[sl_ngroups];
    int i = 0;
    while (name[i] && i < SL_NAME_LEN - 1) { g->name[i] = name[i]; i++; }
    g->name[i] = 0;
    g->tickets = tickets;
    g->threads = threads < 1 ? 1 : (threads > SL_MAX_THREADS ? SL_MAX_THREADS : threads);
    g->kind = kind;
    g->flags = flags;
    g->light_active = light_active;
    g->period_ticks = period;
    g->job_cpu_ticks = job_cpu;
    g->burn = burn;
    g->pid = -1;
    return sl_ngroups++;
}

/* 全程满载 spin 组(子进程) */
static int sl_add_spin(const char *name, int tickets, int threads,
                       unsigned long burn) {
    return sl_add_group(name, tickets, threads, SL_SPIN, 0, 0, 0, 0, burn);
}
/* 三段相位 spin 组(子进程):中段全活跃,轻段只 light_active 个活跃(复刻 40) */
static int sl_add_spin_phased(const char *name, int tickets, int threads,
                              unsigned long burn, int light_active) {
    return sl_add_group(name, tickets, threads, SL_SPIN, SL_F_PHASED,
                        light_active, 0, 0, burn);
}
/* 周期 deadline job 组(独立子进程;mech/对照用) */
static int sl_add_jobs(const char *name, int tickets, int threads,
                       int period_ticks, int job_cpu_ticks, unsigned long burn) {
    return sl_add_group(name, tickets, threads, SL_JOBS, 0, 0,
                        period_ticks, job_cpu_ticks, burn);
}
/* 周期 deadline job 组(跑在监控进程内,统计共享,控制器可读) */
static int sl_add_jobs_parent(const char *name, int tickets, int threads,
                              int period_ticks, int job_cpu_ticks,
                              unsigned long burn) {
    return sl_add_group(name, tickets, threads, SL_JOBS, SL_F_IN_PARENT, 0,
                        period_ticks, job_cpu_ticks, burn);
}

/* ================= 负载执行 ================= */

static void sl_burn(unsigned long iters) {
    volatile unsigned long x = 1;
    for (unsigned long i = 0; i < iters; i++) x = x * 1664525UL + 1013904223UL;
    (void)x;
}

typedef struct { const sl_group_t *g; int idx; } sl_task_arg_t;
static sl_task_arg_t sl_args[SL_MAX_GROUPS][SL_MAX_THREADS];

/* 四段相位:0 轻 / 1 重 / 2 轻 / 3 重(轻重轻重) */
static int sl_phase_now(void) {
    unsigned long span = sl_t_end - sl_t0;
    unsigned long now = get_ticks();
    unsigned long off = now > sl_t0 ? now - sl_t0 : 0;
    if (sl_l_ratio_permil > 0) {
        /* 非等分: 每个L-H周期各占 span/2 */
        unsigned long half = span / 2;
        unsigned long l_seg = half * (unsigned long)sl_l_ratio_permil / 1000;
        if (off < l_seg) return 0;        /* L1 */
        if (off < half) return 1;         /* H1 */
        if (off < half + l_seg) return 2; /* L2 */
        return 3;                          /* H2 */
    }
    /* 等分(默认) */
    unsigned long seg = span / 4;
    int ph = seg ? (int)(off / seg) : 0;
    if (ph > 3) ph = 3;
    return ph;
}

/* 该线程此刻是否该干活;不该干时睡到下一相位边界。返回 0=干活。
 * idx < light_active 的线程轻相位保底活跃;主线程(idx=-1)按普通超编线程
 * 处理,轻相位也睡——轻负载恰好 = light_active 个线程,不多不少。 */
static long sl_phased_sleep(const sl_group_t *g, int idx) {
    if (!(g->flags & SL_F_PHASED)) return 0;
    if (idx >= 0 && idx < g->light_active) return 0;
    int ph = sl_phase_now();
    if (ph == 1 || ph == 3) return 0;         /* 重相位全员活跃 */
    /* 轻相位(0,2):睡到下一边界 */
    unsigned long span = sl_t_end - sl_t0;
    unsigned long boundary;
    if (sl_l_ratio_permil > 0) {
        unsigned long half = span / 2;
        unsigned long l_seg = half * (unsigned long)sl_l_ratio_permil / 1000;
        boundary = sl_t0 + (ph == 0 ? l_seg : half + l_seg);
    } else {
        unsigned long seg = span / 4;
        boundary = sl_t0 + (unsigned long)(ph + 1) * seg;
    }
    long delta = (long)boundary - (long)get_ticks();
    return delta > 0 ? delta : 1;
}

static void sl_spin_fn(void *p) {
    const sl_group_t *g = ((sl_task_arg_t *)p)->g;
    int idx = ((sl_task_arg_t *)p)->idx;
    int gi = (int)(g - sl_groups);
    while (get_ticks() < sl_t_end) {
        long zzz = sl_phased_sleep(g, idx);
        if (zzz > 0) { sleep((usize)zzz); continue; }
        sl_burn(g->burn);
        __sync_fetch_and_add(&sl_gstats[gi].work, 1);
    }
    thread_exit(0);
}

static void sl_job_fn(void *p) {
    const sl_group_t *g = ((sl_task_arg_t *)p)->g;
    int gi = (int)(g - sl_groups);
    sl_gstats[gi].resp_min = (unsigned long)-1;   /* 多线程竞写哨兵,最后一次写生效,无碍 */
    unsigned long release = get_ticks();
    while (get_ticks() < sl_t_end) {
        sl_burn(g->burn);
        unsigned long finish = get_ticks();
        unsigned long deadline = release + (unsigned long)g->period_ticks;
        unsigned long resp = finish - release;
        __sync_fetch_and_add(&sl_gstats[gi].jobs, 1);
        if (finish > deadline) {
            unsigned long late = finish - deadline;
            __sync_fetch_and_add(&sl_gstats[gi].miss, 1);
            __sync_fetch_and_add(&sl_gstats[gi].late_sum, late);
            if (late > sl_gstats[gi].late_max) sl_gstats[gi].late_max = late; /* 竞态,参考 */
        }
        __sync_fetch_and_add(&sl_gstats[gi].resp_sum, resp);
        __sync_fetch_and_add(&sl_gstats[gi].resp_sumsq, resp * resp);
        if (resp < sl_gstats[gi].resp_min) sl_gstats[gi].resp_min = resp;     /* 竞态,参考 */
        if (resp > sl_gstats[gi].resp_max) sl_gstats[gi].resp_max = resp;     /* 竞态,参考 */
        release += (unsigned long)g->period_ticks;
        long ahead = (long)release - (long)get_ticks();
        if (ahead > 0) sleep((usize)ahead);
    }
    thread_exit(0);
}

static void sl_print_j(int gi) {
    sl_group_t *g = &sl_groups[gi];
    sl_gstats_t *s = &sl_gstats[gi];
    unsigned long rmin = s->resp_min == (unsigned long)-1 ? 0 : s->resp_min;
    printf("J,%d,%s,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
           g->pid, g->name, g->threads,
           s->jobs, s->miss, s->late_sum, s->late_max,
           s->resp_sum, s->resp_sumsq, rmin, s->resp_max);
}

static void sl_child_main(sl_group_t *g) {
    int gi = (int)(g - sl_groups);
    set_my_tickets(g->tickets);

    int tids[SL_MAX_THREADS];
    for (int i = 0; i < g->threads; i++) {
        sl_args[gi][i].g = g;
        sl_args[gi][i].idx = i;
        tids[i] = thread_create(g->kind == SL_JOBS ? sl_job_fn : sl_spin_fn,
                                &sl_args[gi][i]);
    }

    /* 主线程不参与负载,只等子线程退出。
     * 这样 runnable = g->threads(主线程 block 在 join,不算 Ready/Running),
     * 符合实验"N threads"的语义。
     * 原来主线程也跑 sl_spin_fn,导致 threads.len=N+1,runnable=N+1。 */
    for (int i = 0; i < g->threads; i++) {
        int code;
        thread_join(tids[i], &code);
    }

    /* 自报汇总 */
    if (g->kind == SL_JOBS) sl_print_j(gi);
    else printf("K,%d,%s,%d,%lu\n", g->pid, g->name, g->threads,
                sl_gstats[gi].work);
    exit(0);
}

/* ================= 策略:AIMD(逐行移植 40_dynamic_load_exp.c) ================= */

typedef struct {
    int alpha;
    int inc;             /* AIMD_INC = 5 */
    int backoff;         /* AIMD_BACKOFF = 80(%) */
    int safe_lateness;   /* 窗口迟到 <= 此值算安全 = 0 */
    int danger_lateness; /* 窗口迟到 >= 此值算危险 = 25 */
    int safe_windows;
    int cooldown;        /* COOLDOWN_WINDOWS_AFTER_DOWN = 1 */
} sl_aimd_t;

static void sl_aimd_init(sl_aimd_t *a, int alpha0) {
    a->alpha = alpha0;
    a->inc = 5;
    a->backoff = 80;
    a->safe_lateness = 0;  
    a->danger_lateness = 25;
    a->safe_windows = 0;
    a->cooldown = 0;
}


static int sl_policy_aimd(const sl_window_t *w, void *ud) {
    sl_aimd_t *a = (sl_aimd_t *)ud;
    unsigned long miss_per_1000 = w->jobs_delta
        ? w->miss_delta * 1000 / w->jobs_delta : 0;
    int can_probe_up = w->remain_windows > 3;   /* MIN_REMAIN_WINDOWS_TO_PROBE */

    int before = a->alpha;
    const char *action = "hold";

    if (a->cooldown > 0) {
        a->cooldown--;
        a->safe_windows = 0;
        action = "cool";
    } else if ((long)w->late_delta >= a->danger_lateness) {
        int na;
        if (miss_per_1000 >= 900)      na = a->alpha * 40 / 100;
        else if (miss_per_1000 >= 500) na = a->alpha * 60 / 100;
        else                           na = a->alpha * a->backoff / 100;
        if (na >= a->alpha) na = a->alpha - 1;
        if (na < 0) na = 0;
        a->alpha = na;
        a->safe_windows = 0;
        a->cooldown = 3;         /* 原 1：down 后冷却 3 窗，减少退避抖动 → 减少 set_sched_alpha 调用 */
        action = "down";
    } else if ((long)w->late_delta <= a->safe_lateness) {
        a->safe_windows++;
        /* 原 >=1：连续 2 个安全窗才 up，减少 α 微调频率 → 减少 set_sched_alpha 调用。
         * 用 2 不用 3：late<=0 窗口仅 ~26%，>=3 要连续 3 个，AIMD 可能爬不到 60。 */
        if (a->safe_windows >= 2 && can_probe_up) {
            int na = a->alpha + a->inc;
            if (na > 100) na = 100;
            a->alpha = na;
            a->safe_windows = 0;
            action = "up";
        } else {
            action = "hold";
        }
    } else {
        a->safe_windows = 0;
        action = "gray";
    }

    printf("A,%d,%d,%d,%s\n", w->window_no, before, a->alpha, action);
    return a->alpha;
}


/* ================= 策略:SPSA-AdamW(deadline 损失版) =================
 * loss_q = miss_per_1000 + 平均迟到 ×1000(封顶 4000)。
 * 与 AIMD 同一信号竞技;其余 SPSA/定点机制与 v1 相同。
 * 注意:ctrl 空闲的窗口 loss→0、梯度≈0,alpha 被 weight decay 拉回 target=25,
 * 不像 AIMD 会主动爬高——这是两种控制器的本质差异之一(定理 3: 安全区漂移,
 * exp7 的 adamw vs 三 ablation 直接对照; 但 in-parent late 基线非零, "纯平坦区"
 * 罕见, 见 docs/theory_alpha.md §5 先验修正)。
 */
typedef struct {
    long long m, v;
    long long t;
    int alpha;
    int alpha_f;       /* ×1024 */
    int lr;            /* 稳态步长,alpha 点/窗口 */
    int target;        /* weight decay 目标 */
    int delta;         /* SPSA 扰动 = 5 */
    int prev_probe;
    long long prev_loss;
    /* 定理3(decay 漂移)诊断分量: 每窗更新后可从 A 行直接读出
     * 安全区(loss=0)内 step→0、decay=(target*1024-alpha_f)*2/100 恒定,
     * alpha_f-25 应呈几何衰减, 时间常数 50 窗。 */
    long long dbg_loss, dbg_g, dbg_step, dbg_decay;
} sl_adamw_t;

static void sl_adamw_init(sl_adamw_t *s, int alpha0, int lr, int target) {
    s->m = s->v = 0; s->t = 0;
    s->alpha = alpha0; s->alpha_f = alpha0 * 1024;
    s->lr = lr; s->target = target; s->delta = 5;
    s->prev_probe = 0; s->prev_loss = -1;
    s->dbg_loss = -1; s->dbg_g = 0; s->dbg_step = 0; s->dbg_decay = 0;
}

static long long sl_deadline_loss(const sl_window_t *w) {
    unsigned long miss_per_1000 = w->jobs_delta
        ? w->miss_delta * 1000 / w->jobs_delta : 0;
    unsigned long late_per_job = w->jobs_delta
        ? w->late_delta * 1000 / w->jobs_delta : 0;
    if (late_per_job > 4000) late_per_job = 4000;
    return (long long)(miss_per_1000 + late_per_job);
}

static int sl_policy_adamw(const sl_window_t *w, void *ud) {
    sl_adamw_t *s = (sl_adamw_t *)ud;
    long long loss = sl_deadline_loss(w);
    int before = s->alpha;

    if (s->prev_probe != 0 && s->prev_loss >= 0) {
        long long g = (loss - s->prev_loss) * 1024
                      / (2 * s->delta * s->prev_probe);
        s->t++;
        s->m = s->m * 9 / 10 + g / 10;
        s->v = s->v * 99 / 100 + g * g / 100;
        long long denom = 1;
        {   /* isqrt(v) */
            long long r = 0, bit = 1LL << 30, vv = s->v;
            while (bit > vv) bit >>= 2;
            while (bit) {
                if (vv >= r + bit) { vv -= r + bit; r = (r >> 1) + bit; }
                else r >>= 1;
                bit >>= 2;
            }
            denom = r > 1 ? r : 1;
        }
        long long step = (long long)s->lr * 1024 * s->m / denom;
        long long decay = (long long)(s->target * 1024 - s->alpha_f) * 2 / 100;
        s->dbg_g = g; s->dbg_step = step; s->dbg_decay = decay;
        s->alpha_f -= (int)(step - decay);
        if (s->alpha_f < 0)      s->alpha_f = 0;
        if (s->alpha_f > 102400) s->alpha_f = 102400;
        s->alpha = (int)(s->alpha_f / 1024);
    }
    s->dbg_loss = loss;

    if      (s->alpha + s->delta > 100) s->prev_probe = -1;
    else if (s->alpha - s->delta < 0)   s->prev_probe =  1;
    else s->prev_probe = (w->window_no & 1) ? 1 : -1;
    s->prev_loss = loss;

    /* A 行: 输出 AdamW 的 α 决策轨迹 + 定理3分解列
     * before=更新前 α, after=更新后 α(实际值,不含 SPSA 扰动)
     * action: up/down/hold; loss/g/step/decay 均为 ×1024 定点原值
     * (step/decay 在无更新窗输出 0; 首窗 dbg_loss 从 -1 起)。
     * 定理3检验: 取 loss=0 连续段, 回归 ln(alpha_f/1024-25) 对窗口的斜率 ≈ -2%。 */
    const char *action;
    if (s->alpha > before)      action = "up";
    else if (s->alpha < before) action = "down";
    else                        action = "hold";
    printf("A,%d,%d,%d,%s,%lld,%lld,%lld,%lld\n", w->window_no, before, s->alpha,
           action, s->dbg_loss, s->dbg_g, s->dbg_step, s->dbg_decay);

    int probe = s->alpha + s->prev_probe * s->delta;
    if (probe < 0) probe = 0;
    if (probe > 100) probe = 100;
    return probe;
}

/* ================= 整数平方根/立方根(CUBIC 与 UCB 共用, x>=0) ================= */

static long long sl_ll_isqrt(long long x) {
    long long r = 0, bit = 1LL << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static long long sl_ll_icbrt(long long x) {
    if (x <= 0) return 0;
    long long lo = 0, hi = 1;
    while (hi * hi * hi <= x) hi <<= 1;   /* x<=5.4e11 时 hi<=2^20, hi^3 不溢出 */
    while (lo < hi - 1) {
        long long mid = (lo + hi) / 2;
        if (mid * mid * mid <= x) lo = mid; else hi = mid;
    }
    return lo;
}

/* ================= 策略:CUBIC(TCP 拥塞控制移植, RFC 8312 思路) =================
 * 对照 AIMD(=Reno 血统)的线性爬升,CUBIC 用三次曲线:
 *   退避(事件同 AIMD: late_delta >= danger): α <- α*β, 记 w_max = 退避前 α
 *   爬升: W(t) = C*(t-K)^3 + w_max,  K = cbrt(w_max*(1-β)/C),  t = 距退避窗数
 * 特征: 远离 w_max 快速收复、接近 w_max 减速缓行、越过 w_max 继续试探。
 * 预期(诚实假设): α 值域只有 0-100(远小于 TCP cwnd),收复速度与 AIMD 相近;
 *   真正可测的差异是"逼近危险区减速" → 过冲更少 → A 行 down 次数更少。
 * RFC 的 TCP-friendly/Hystart 全省略——单变量纯 CUBIC。
 * 定点: α/W/t/K 均 ×1024; d=(t-K) 钳 ±32768 防 t 长期无退避时立方溢出
 *   (32768^3*61 ≈ 2.2e15, long long 安全)。
 * 注意: CUBIC 每窗都在变 α → set_sched_alpha 每窗一次
 *   (AdamW 的 SPSA probe 也是如此, 无新增开销等级)。
 */
typedef struct {
    long long alpha_f;    /* ×1024 */
    long long w_max_f;    /* 本纪元退避前 α, ×1024 */
    long long t_f;        /* 距上次退避窗口数, ×1024 (每窗 +1024) */
    long long k_f;        /* K, ×1024 */
    int beta_pct;         /* 70 */
    int c_fp;             /* C ×1024, 默认 61 (C=0.06: w_max=100 时 K≈8 窗) */
    int danger_lateness;  /* 25, 同 AIMD */
} sl_cubic_t;

static void sl_cubic_init(sl_cubic_t *c, int alpha0) {
    c->alpha_f = (long long)alpha0 * 1024;
    c->w_max_f = c->alpha_f;
    c->t_f = 0;
    c->k_f = 0;
    c->beta_pct = 70;
    c->c_fp = 61;
    c->danger_lateness = 25;
}

static int sl_policy_cubic(const sl_window_t *w, void *ud) {
    sl_cubic_t *c = (sl_cubic_t *)ud;
    int before = (int)(c->alpha_f / 1024);
    const char *action;

    if ((long)w->late_delta >= c->danger_lateness) {
        /* 乘性退避 + 开新纪元 */
        c->w_max_f = c->alpha_f;
        c->alpha_f = c->alpha_f * c->beta_pct / 100;
        c->t_f = 0;
        /* K³ = w_max*(1-β)/C  →  k_f³ = deficit_f * 2^30 / c_fp */
        long long deficit_f = c->w_max_f - c->alpha_f;
        c->k_f = sl_ll_icbrt(deficit_f * (1LL << 30) / c->c_fp);
        action = "down";
    } else {
        /* 三次爬升: W(t) = C*(t-K)^3 + w_max (t<K 时 W<w_max, 单调回升) */
        c->t_f += 1024;
        long long d = c->t_f - c->k_f;
        if (d > 32768) d = 32768;
        if (d < -32768) d = -32768;
        c->alpha_f = c->w_max_f + c->c_fp * d * d * d / (1LL << 30);
        action = "up";
    }
    if (c->alpha_f < 0)      c->alpha_f = 0;
    if (c->alpha_f > 102400) c->alpha_f = 102400;
    int alpha = (int)(c->alpha_f / 1024);
    if (alpha == before && action[0] != 'd') action = "hold";
    printf("A,%d,%d,%d,%s\n", w->window_no, before, alpha, action);
    return alpha;
}

/* ================= 策略:SW-UCB(滑动窗口多臂老虎机, Garivier & Moulines 2011) =================
 * α 离散成 11 档臂(0,10,...,100),每窗口整臂调度,reward = 10000 - loss。
 * 选臂: score = mean_i + c * sqrt(2 ln t / n_i),冷启动逐臂试一遍。
 * 为什么滑窗: 纯 UCB1 对全历史平均,相位切换后旧数据污染(每相位仅
 *   240~960 窗) → 每臂只统计最近 TAU=32 个样本。
 * 诚实局限(写进报告): bandit 假设 reward 只依赖所选臂,实际还依赖相位
 *   上下文——UCB 的落后正是"无上下文"的代价,是讨论点不是失败。
 * c_explore 与 reward 同量纲(loss 尺度 0..5000, 臂间均值差 ~百-千),
 *   默认 1000,可调;reward=10000-loss 保证非负,均值差不变。
 */
#define SL_UCB_ARMS 11   /* α 臂: 0,10,...,100 */
#define SL_UCB_TAU  32   /* 滑窗长度(每臂保留样本数) */

typedef struct {
    long long ring[SL_UCB_ARMS][SL_UCB_TAU];
    int head[SL_UCB_ARMS];
    int n[SL_UCB_ARMS];      /* 有效样本数, <= TAU */
    long long sum[SL_UCB_ARMS];
    long long t;             /* 总窗口数 */
    int last_arm;            /* 上一窗实际执行的臂(初值 = alpha0/10) */
    int cold;                /* 冷启动游标 */
    int c_explore;
} sl_ucb_t;

static void sl_ucb_init(sl_ucb_t *u, int alpha0) {
    for (int i = 0; i < SL_UCB_ARMS; i++) {
        u->head[i] = 0; u->n[i] = 0; u->sum[i] = 0;
        for (int j = 0; j < SL_UCB_TAU; j++) u->ring[i][j] = 0;
    }
    u->t = 0;
    u->cold = 0;
    u->last_arm = (alpha0 >= 0 && alpha0 <= 100 && alpha0 % 10 == 0)
                  ? alpha0 / 10 : -1;
    u->c_explore = 1000;
}

static int sl_policy_ucb(const sl_window_t *w, void *ud) {
    sl_ucb_t *u = (sl_ucb_t *)ud;
    long long reward = 10000 - sl_deadline_loss(w);

    /* 1. 用本窗观测更新上一窗实际执行的臂(滑窗: 满则淘汰最老样本)。
     * 时序: 窗 N 的 stats 是窗 N 实际执行的 α(last_arm)产生的。 */
    int la = u->last_arm;
    if (la >= 0) {
        if (u->n[la] == SL_UCB_TAU) u->sum[la] -= u->ring[la][u->head[la]];
        else u->n[la]++;
        u->ring[la][u->head[la]] = reward;
        u->sum[la] += reward;
        u->head[la] = (u->head[la] + 1) % SL_UCB_TAU;
    }
    u->t++;

    /* 2. 选下一臂: 冷启动逐臂试一遍, 之后 SW-UCB1 分数 */
    int best;
    if (u->cold < SL_UCB_ARMS) {
        best = u->cold++;
    } else {
        long long ln_fp = 0, x = u->t;      /* ln(t) ≈ floor(log2 t)*ln2, ×1024 */
        while (x > 1) { x >>= 1; ln_fp += 1024; }
        ln_fp = ln_fp * 710 / 1024;
        long long best_score = -1;
        best = 0;
        for (int i = 0; i < SL_UCB_ARMS; i++) {
            if (u->n[i] == 0) { best = i; break; }   /* 防御: 未试过的臂优先 */
            long long mean = u->sum[i] / u->n[i];
            long long sq = sl_ll_isqrt(2 * ln_fp * 1024 / u->n[i]); /* sqrt(2lnt/n)*1024 */
            long long score = mean + u->c_explore * sq / 1024;
            if (score > best_score) { best_score = score; best = i; }
        }
    }
    u->last_arm = best;
    int alpha = best * 10;
    printf("A,%d,%d,%d,arm%d\n", w->window_no, w->alpha, alpha, best);
    return alpha;
}

/* ================= 策略:SPSA 优化器家族(SGD-M / RMSProp / AdaGrad) =================
 * AdamW 的 ablation 对照: 与 sl_policy_adamw 共用同一 SPSA 梯度估计与
 * 同一 loss(sl_deadline_loss), 只换更新公式, 分离各组件贡献:
 *   SGD-M  : m=0.9m+0.1g,           step=lr*m/G_REF   (只有动量, 无自适应)
 *   RMSProp: v=0.99v+0.01g²,        step=lr*g/isqrt(v)(只有自适应, 无动量)
 *   AdaGrad: v+=g² (不衰减累积),    step=lr*g/isqrt(v)(自适应但步长单调萎缩)
 *   AdamW(=动量+自适应+weight decay, 见 sl_policy_adamw) —— exp7 同批重跑,
 *   三个 ablation 与它的差 = 各组件/decay 的净贡献。
 * 全家均无 weight decay —— decay 的贡献由 adamw vs 三兄弟直接读出。
 *
 * 量纲对齐(公平关键): G_REF=10240 是 SPSA 在 delta=5 时的典型 |g|
 *   (|g|≈Δloss×102.4, 相邻窗 Δloss~100 → |g|~10240), 记为"单位梯度"。
 *   四个优化器稳态步长都对齐到 ~lr 点/窗, 差异只能来自更新公式本身,
 *   不是谁步长天生大。SGD-M 无自适应、对梯度量级敏感——这是要观测的
 *   差异, 故只加 ±32768 裁剪防相位切换时 g 爆量程(工程标准做法)。
 * 溢出: AdaGrad v 累积 2400 窗 × (32768)² ≈ 2.6e12, long long 安全。
 */
#define SL_OPTIM_SGDM    0
#define SL_OPTIM_RMSPROP 1
#define SL_OPTIM_ADAGRAD 2
#define SL_G_REF         10240

typedef struct {
    int kind;           /* SL_OPTIM_* */
    long long m, v, t;
    int alpha;
    int alpha_f;        /* ×1024 */
    int lr;             /* 同 AdamW: 稳态步长, alpha 点/窗口 */
    int delta;          /* SPSA 扰动 = 5, 同 AdamW */
    int prev_probe;
    long long prev_loss;
    /* 定理3反事实诊断: 与 AdamW 同格式, decay 恒 0 —— 安全区(loss=0)
     * 内若无 decay 则 alpha 应冻结(step→0 且无漂移项), 与 AdamW 的
     * 几何漂移形成直接对照。 */
    long long dbg_loss, dbg_g, dbg_step, dbg_decay;
} sl_optim_t;

static void sl_optim_init(sl_optim_t *s, int kind, int alpha0, int lr) {
    s->kind = kind;
    s->m = s->v = 0; s->t = 0;
    s->alpha = alpha0; s->alpha_f = alpha0 * 1024;
    s->lr = lr; s->delta = 5;
    s->prev_probe = 0; s->prev_loss = -1;
    s->dbg_loss = -1; s->dbg_g = 0; s->dbg_step = 0; s->dbg_decay = 0;
}

static int sl_policy_optim(const sl_window_t *w, void *ud) {
    sl_optim_t *s = (sl_optim_t *)ud;
    long long loss = sl_deadline_loss(w);
    int before = s->alpha;

    if (s->prev_probe != 0 && s->prev_loss >= 0) {
        long long g = (loss - s->prev_loss) * 1024
                      / (2 * s->delta * s->prev_probe);
        if (g >  32768) g =  32768;
        if (g < -32768) g = -32768;
        s->t++;
        long long step = 0;
        if (s->kind == SL_OPTIM_SGDM) {
            s->m = s->m * 9 / 10 + g / 10;
            step = (long long)s->lr * 1024 * s->m / SL_G_REF;
        } else if (s->kind == SL_OPTIM_RMSPROP) {
            s->v = s->v * 99 / 100 + g * g / 100;
            long long denom = sl_ll_isqrt(s->v); if (denom < 1) denom = 1;
            step = (long long)s->lr * 1024 * g / denom;
        } else {   /* SL_OPTIM_ADAGRAD */
            s->v += g * g;
            long long denom = sl_ll_isqrt(s->v); if (denom < 1) denom = 1;
            step = (long long)s->lr * 1024 * g / denom;
        }
        s->dbg_g = g; s->dbg_step = step; s->dbg_decay = 0;
        s->alpha_f -= (int)step;
        if (s->alpha_f < 0)      s->alpha_f = 0;
        if (s->alpha_f > 102400) s->alpha_f = 102400;
        s->alpha = s->alpha_f / 1024;
    }
    s->dbg_loss = loss;

    if      (s->alpha + s->delta > 100) s->prev_probe = -1;
    else if (s->alpha - s->delta < 0)   s->prev_probe =  1;
    else s->prev_probe = (w->window_no & 1) ? 1 : -1;
    s->prev_loss = loss;

    /* A 行: 与 AdamW 同格式(decay 恒 0), 便于 stat 端统一解析对照 */
    const char *action;
    if (s->alpha > before)      action = "up";
    else if (s->alpha < before) action = "down";
    else                        action = "hold";
    printf("A,%d,%d,%d,%s,%lld,%lld,%lld,%lld\n", w->window_no, before, s->alpha,
           action, s->dbg_loss, s->dbg_g, s->dbg_step, s->dbg_decay);

    int probe = s->alpha + s->prev_probe * s->delta;
    if (probe < 0) probe = 0;
    if (probe > 100) probe = 100;
    return probe;
}

/* ================= 策略:增量式 PI(控制论经典, 对照靶子) =================
 * 误差 e = late_delta − target(迟到超过目标为正)。
 * 增量式: Δα = −( kp·Δe + ki·e )/1024, α 本身就是积分器,
 *   [0,100] 钳位即条件积分法 anti-windup(教科书做法, 无需显式 I 管理)。
 * 参数标定(每个都能一句话说清):
 *   kp_fp=150(0.146): 相位切换 Δe≈+90 时首窗猛降 ~13 点(等效 D 的阻尼)
 *   ki_fp=64(0.0625): 重相位 e≈90 时持续 −5.6 点/窗, 轻相位 e=−10 时
 *                     +0.6 点/窗慢爬 —— 与 AIMD 的 +5/窗 同向但无死区
 *   target=10: 允许轻微迟到(对齐 AIMD 的 safe=0/danger=25 中间偏安全)
 * 诚实假设: 恒定段比 AIMD 平滑(连续调节无死区); 相位切换后 e_prev 的
 *   惯性仍在(Δe 项反向拖拽), 预期恢复慢于 AIMD 的"退避+冷却"。
 * PID 在资源调度的文献已多(control-theoretic scheduling)——定位是
 *   对照组: "教科书控制器 vs 领域启发式"谁更适合 deadline 场景。
 */
typedef struct {
    int alpha_f;    /* ×1024 */
    int e_prev;
    int kp_fp;      /* ×1024 */
    int ki_fp;      /* ×1024 */
    int target;
} sl_pid_t;

static void sl_pid_init(sl_pid_t *p, int alpha0, int kp_fp, int ki_fp, int target) {
    p->alpha_f = alpha0 * 1024;
    p->e_prev = -target;      /* 假设首窗前 late=0, 避免首窗假 Δe */
    p->kp_fp = kp_fp; p->ki_fp = ki_fp; p->target = target;
}

static int sl_policy_pid(const sl_window_t *w, void *ud) {
    sl_pid_t *p = (sl_pid_t *)ud;
    int before = p->alpha_f / 1024;
    int e = (int)w->late_delta - p->target;
    long long d = -(long long)p->kp_fp * (e - p->e_prev)
                  - (long long)p->ki_fp * e;      /* Δα, ×1024 */
    p->e_prev = e;
    p->alpha_f += (int)(d / 1024);
    if (p->alpha_f < 0)      p->alpha_f = 0;
    if (p->alpha_f > 102400) p->alpha_f = 102400;
    int alpha = p->alpha_f / 1024;
    const char *action = alpha > before ? "up" : (alpha < before ? "down" : "hold");
    printf("A,%d,%d,%d,%s\n", w->window_no, before, alpha, action);
    return alpha;
}

/* ================= 监控与运行 ================= */

static int sl_abs_i(int x) { return x < 0 ? -x : x; }

static void sl_measure_window(sl_window_t *w, int win_no, int alpha,
                              unsigned long prev_run[],
                              unsigned long prev_jobs[],
                              unsigned long prev_miss[],
                              unsigned long prev_late[]) {
    int total_eff = 0, total_run = 0;
    w->window_no = win_no; w->alpha = alpha;
    w->nprocs = 0; w->max_slowdown_q = 0;
    w->jobs_delta = w->miss_delta = w->late_delta = 0;

    for (int i = 0; i < sl_ngroups; i++) {
        struct sched_proc_stat st;
        sl_group_t *g = &sl_groups[i];
        if (g->pid < 0) continue;
        if (get_process_sched_stat(g->pid, &st) < 0) continue;

        sl_proc_t *p = &w->procs[w->nprocs];
        p->pid = g->pid;
        for (int c = 0; c < SL_NAME_LEN; c++) p->name[c] = g->name[c];
        p->eff_tickets = st.effective_tickets;
        p->ready_threads = st.ready_threads;
        p->run_delta = st.run_ticks - prev_run[i];
        prev_run[i] = st.run_ticks;
        if (p->ready_threads > 0)
            total_eff += p->eff_tickets > 0 ? p->eff_tickets : 1;
        total_run += (int)p->run_delta;
        w->nprocs++;

        printf("W,%d,%d,%d,%s,%lu,%d,%d\n",
               win_no, alpha, p->pid, p->name,
               p->run_delta, p->eff_tickets, p->ready_threads);

        /* in-parent jobs 组:共享计数器差分(控制器反馈信号) */
        if (g->flags & SL_F_IN_PARENT) {
            w->jobs_delta = sl_gstats[i].jobs - prev_jobs[i];
            w->miss_delta = sl_gstats[i].miss - prev_miss[i];
            w->late_delta = sl_gstats[i].late_sum - prev_late[i];
            prev_jobs[i] = sl_gstats[i].jobs;
            prev_miss[i] = sl_gstats[i].miss;
            prev_late[i] = sl_gstats[i].late_sum;
            printf("D,%d,%d,%lu,%lu,%lu\n",
                   win_no, alpha, w->jobs_delta, w->miss_delta, w->late_delta);
        }
    }

    long long sum = 0, sumsq = 0;
    for (int i = 0; i < w->nprocs; i++) {
        sl_proc_t *p = &w->procs[i];
        if (p->ready_threads <= 0) {
            p->entitled_q = 0; p->share_q = 0; p->slowdown_q = 1000;
            continue;
        }
        p->entitled_q = total_eff ? p->eff_tickets * 1000 / total_eff : 0;
        p->share_q = total_run ? (int)p->run_delta * 1000 / total_run : 0;
        p->slowdown_q = p->share_q > 0
            ? p->entitled_q * 1000 / p->share_q
            : (p->entitled_q > 0 ? 9999 : 1000);
        long long r = p->share_q - p->entitled_q;
        sum += sl_abs_i((int)r);
        sumsq += r * r;
        if (p->slowdown_q > w->max_slowdown_q)
            w->max_slowdown_q = p->slowdown_q;
    }
    if (w->nprocs > 0 && sumsq > 0)
        w->jain_q = (int)(sum * sum * 1000 / (sumsq * w->nprocs));
    else
        w->jain_q = 1000;
}

static int sl_run(const sl_cfg *cfg) {
    sl_window = cfg->window_ticks > 0 ? cfg->window_ticks : 100;
    int alpha = cfg->alpha0;
    if (alpha < 0 || alpha > 100) alpha = 50;
    unsigned long start_delay = cfg->start_delay ? cfg->start_delay : 80;

    reset_sched_stat();
    set_sched_alpha(0);
    sl_t0 = get_ticks();
    sl_t_end = sl_t0 + cfg->total_ticks;


    unsigned long prev_run[SL_MAX_GROUPS]  = {0};
    unsigned long prev_jobs[SL_MAX_GROUPS] = {0};
    unsigned long prev_miss[SL_MAX_GROUPS] = {0};
    unsigned long prev_late[SL_MAX_GROUPS] = {0};
    printf("# prev_baseline_reset ngroups=%d\n", sl_ngroups);  /* 哨兵,见下 */


    for (int i = 0; i < sl_ngroups; i++) {
        sl_group_t *g = &sl_groups[i];
        if (g->flags & SL_F_IN_PARENT) {
            g->pid = (int)getpid();
            set_my_tickets(g->tickets);
            for (int t = 0; t < g->threads; t++) {
                sl_args[i][t].g = g;
                sl_args[i][t].idx = t;
                thread_create(sl_job_fn, &sl_args[i][t]);
            }
            continue;
        }
        int pid = (int)fork();
        if (pid == 0) sl_child_main(g);
        g->pid = pid;
    }


    long d0 = (long)(sl_t0 + start_delay) - (long)get_ticks();
    if (d0 > 0) sleep((usize)d0);

    /* 切到实验 alpha,重置统计和时间基线。
     * 创建阶段(alpha=0)的数据不计入实验结果。 */
    set_sched_alpha(alpha);
    reset_sched_stat();
    sl_t0 = get_ticks();                 // ← 重置 t0,窗口从这里开始
    sl_t_end = sl_t0 + cfg->total_ticks;

    printf("# schedlab win=%d total=%lu groups=%d alpha=%d\n",
           sl_window, cfg->total_ticks, sl_ngroups, alpha);

    for (int i = 0; i < sl_ngroups; i++) {
        if (sl_groups[i].pid < 0) continue;
        struct sched_proc_stat st;
        if (get_process_sched_stat(sl_groups[i].pid, &st) >= 0) {
            prev_run[i] = st.run_ticks;
        }
        if (sl_groups[i].flags & SL_F_IN_PARENT) {
            prev_jobs[i] = sl_gstats[i].jobs;
            prev_miss[i] = sl_gstats[i].miss;
            prev_late[i] = sl_gstats[i].late_sum;
        }
    }

    int win = 0;
    while (get_ticks() < sl_t_end) {
        sleep((usize)sl_window);
        win++;
        static sl_window_t w;          /* 这个 static 留着无所谓,w 每窗口被整体重写 */
        w.remain_windows = (int)((sl_t_end > get_ticks()
            ? sl_t_end - get_ticks() : 0) / (unsigned long)sl_window);
        sl_measure_window(&w, win, alpha, prev_run, prev_jobs, prev_miss, prev_late);
        if (cfg->policy) {
            int new_alpha = cfg->policy(&w, cfg->policy_ud);
            if (new_alpha < 0) new_alpha = 0;
            if (new_alpha > 100) new_alpha = 100;
            if (new_alpha != alpha) {         // ← 只在变化时才调
                alpha = new_alpha;
                set_sched_alpha(alpha);
            }
        }
        printf("S,%d,%d,%d,%d\n", win, alpha, w.jain_q, w.max_slowdown_q);
    }

    /* in-parent 组的 J 行由监控进程自报 */
    for (int i = 0; i < sl_ngroups; i++) {
        if ((sl_groups[i].flags & SL_F_IN_PARENT) &&
            sl_groups[i].kind == SL_JOBS) {
            sl_print_j(i);
        }
    }

    int code;
    for (int i = 0; i < sl_ngroups; i++) {
        if (sl_groups[i].flags & SL_F_IN_PARENT) continue;
        waitpid(sl_groups[i].pid, &code, 0);
    }
    printf("# done\n");
    sleep(2);
    return 0;
}

#ifdef __cplusplus
}
#endif