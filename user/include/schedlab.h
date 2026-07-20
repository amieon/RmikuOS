/*
 * schedlab.h —— RmikuOS 调度实验框架(v1,静态审计后,待真机首编)
 *
 * 用法:
 *   #include "schedlab.h"
 *   int main(void) {
 *       sl_add_jobs("ctrl", 300, 8, 4, 2, 400000);
 *       sl_add_spin("ai",   100, 32, 12000);
 *       sl_adamw_init(&st, 50, 8, 50);
 *       sl_run(&(sl_cfg){ .total_ticks=36000, .window_ticks=100,
 *                         .alpha0=50, .policy=sl_policy_adamw, .policy_ud=&st });
 *   }
 *
 * 输出(全部原始量,推导交给宿主机 Python):
 *   W,win,alpha,pid,name,run_delta,eff_tickets,ready_threads
 *   J,pid,name,threads,jobs,miss,late_sum,resp_sum,resp_min,resp_max
 *   S,win,next_alpha,jain_q,max_slowdown_q
 *
 * 依赖: 仅 user.h(fork/thread/sched/get_ticks/sleep)。
 * 约定: 策略回调在监控进程中执行;实验期间 alpha 由库独占,
 *       一个窗口内 alpha 冻结(保证"应得份额"测量干净)。
 *
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "user.h"

/* #define printf uprintf */

#define SL_MAX_GROUPS  8
#define SL_MAX_THREADS 64
#define SL_NAME_LEN    16

/* ================= 数据类型 ================= */

typedef enum { SL_SPIN = 0, SL_JOBS = 1 } sl_kind_t;

typedef struct {
    char     name[SL_NAME_LEN];
    int      tickets;
    int      threads;        /* 组内线程数(主线程另算,+1) */
    sl_kind_t kind;
    /* SL_JOBS 参数 */
    int      period_ticks;   /* 释放周期 */
    int      job_cpu_ticks;  /* 单 job 期望占用(记账用,执行只看 burn) */
    /* 两者通用 */
    unsigned long burn;      /* spin: 每轮 burn 迭代;jobs: 单 job burn */
    unsigned long start_delay, run_ticks;  /* 相位控制(0 = 全程) */
    /* 运行时填充 */
    int      pid;
} sl_group_t;

typedef struct {
    int  pid;
    char name[SL_NAME_LEN];
    int  eff_tickets;
    int  ready_threads;
    unsigned long run_delta;   /* 本窗口实跑 ticks */
    int  share_q;              /* 实得份额 ×1000 */
    int  entitled_q;           /* 应得份额 ×1000(就绪组按 eff_tickets 占比) */
    int  slowdown_q;           /* entitled/actual ×1000,理想值 1000 */
} sl_proc_t;

typedef struct {
    int window_no;
    int alpha;
    int nprocs;
    sl_proc_t procs[SL_MAX_GROUPS];
    int jain_q;                /* 偏离度指数 ×1000(越小越公平,非标准 Jain) */
    int max_slowdown_q;        /* 各就绪组 slowdown_q 的最大值 */
} sl_window_t;

/* 策略回调: 根据本窗口观测,返回下一个窗口的 alpha(0..100)。 */
typedef int (*sl_policy_t)(const sl_window_t *w, void *ud);

typedef struct {
    unsigned long total_ticks;
    int           window_ticks;
    int           alpha0;        /* 初始 alpha,默认 50 */
    sl_policy_t   policy;        /* NULL = 固定 alpha0 */
    void         *policy_ud;
} sl_cfg;

/* ================= 全局状态(实验程序单实例,从简) ================= */

static sl_group_t sl_groups[SL_MAX_GROUPS];
static int        sl_ngroups;
static unsigned long sl_t0, sl_t_end;
static int        sl_window = 100;

/* ================= 负载注册 ================= */

static int sl_add_group(const char *name, int tickets, int threads,
                        sl_kind_t kind, int period, int job_cpu,
                        unsigned long burn,
                        unsigned long start_delay, unsigned long run_ticks)
{
    if (sl_ngroups >= SL_MAX_GROUPS) return -1;
    sl_group_t *g = &sl_groups[sl_ngroups];
    int i = 0;
    while (name[i] && i < SL_NAME_LEN - 1) { g->name[i] = name[i]; i++; }
    g->name[i] = 0;
    g->tickets = tickets;
    g->threads = threads < 1 ? 1 : (threads > SL_MAX_THREADS ? SL_MAX_THREADS : threads);
    g->kind = kind;
    g->period_ticks = period;
    g->job_cpu_ticks = job_cpu;
    g->burn = burn;
    g->start_delay = start_delay;
    g->run_ticks = run_ticks;
    g->pid = -1;
    return sl_ngroups++;
}

/* 全程满载 spin 组 */
static int sl_add_spin(const char *name, int tickets, int threads,
                       unsigned long burn) {
    return sl_add_group(name, tickets, threads, SL_SPIN, 0, 0, burn, 0, 0);
}
/* 定时相位 spin 组(三段式动态负载: start_delay 后加入, run_ticks 后退出) */
static int sl_add_spin_phased(const char *name, int tickets, int threads,
                              unsigned long burn,
                              unsigned long start_delay, unsigned long run_ticks) {
    return sl_add_group(name, tickets, threads, SL_SPIN, 0, 0, burn,
                        start_delay, run_ticks);
}
/* 周期 deadline job 组 */
static int sl_add_jobs(const char *name, int tickets, int threads,
                       int period_ticks, int job_cpu_ticks, unsigned long burn) {
    return sl_add_group(name, tickets, threads, SL_JOBS,
                        period_ticks, job_cpu_ticks, burn, 0, 0);
}

/* ================= 负载执行(子进程内) ================= */

static void sl_burn(unsigned long iters) {
    volatile unsigned long x = 1;
    for (unsigned long i = 0; i < iters; i++) x = x * 1664525UL + 1013904223UL;
    (void)x;
}

typedef struct { const sl_group_t *g; int idx; } sl_task_arg_t;
static sl_task_arg_t sl_args[SL_MAX_GROUPS][SL_MAX_THREADS];

/* 各组自己的 job 统计(进程内共享;计数用 AMO 防 SMP 丢失更新,
 * min/max 允许竞态,仅作参考) */
static unsigned long j_jobs, j_miss, j_late_sum, j_resp_sum, j_resp_min, j_resp_max;

static void sl_spin_fn(void *p) {
    const sl_group_t *g = ((sl_task_arg_t *)p)->g;
    unsigned long end = g->run_ticks ? sl_t0 + g->start_delay + g->run_ticks : sl_t_end;
    while (get_ticks() < end) sl_burn(g->burn);
    thread_exit(0);
}

static void sl_job_fn(void *p) {
    const sl_group_t *g = ((sl_task_arg_t *)p)->g;
    unsigned long release = get_ticks();
    while (get_ticks() < sl_t_end) {
        sl_burn(g->burn);
        unsigned long finish = get_ticks();
        unsigned long deadline = release + (unsigned long)g->period_ticks;
        unsigned long resp = finish - release;
        __sync_fetch_and_add(&j_jobs, 1);
        if (finish > deadline) {
            __sync_fetch_and_add(&j_miss, 1);
            __sync_fetch_and_add(&j_late_sum, finish - deadline);
        }
        __sync_fetch_and_add(&j_resp_sum, resp);
        if (resp < j_resp_min) j_resp_min = resp;   /* 竞态允许,参考值 */
        if (resp > j_resp_max) j_resp_max = resp;   /* 竞态允许,参考值 */
        release += (unsigned long)g->period_ticks;
        long ahead = (long)release - (long)get_ticks();
        if (ahead > 0) sleep((usize)ahead);
    }
    thread_exit(0);
}

static void sl_child_main(sl_group_t *g) {
    set_my_tickets(g->tickets);
    if (g->start_delay) sleep((usize)g->start_delay);
    j_resp_min = (unsigned long)-1;
    for (int i = 0; i < g->threads; i++) {
        sl_args[g - sl_groups][i].g = g;
        sl_args[g - sl_groups][i].idx = i;
        thread_create(g->kind == SL_JOBS ? sl_job_fn : sl_spin_fn,
                      &sl_args[g - sl_groups][i]);
    }
    /* 主线程也干活(省一个线程位) */
    if (g->kind == SL_JOBS) sl_job_fn(&(sl_task_arg_t){ g, -1 });
    else                    sl_spin_fn(&(sl_task_arg_t){ g, -1 });
    /* 自报汇总(J 行) */
    if (g->kind == SL_JOBS) {
        if (j_resp_min == (unsigned long)-1) j_resp_min = 0;
        printf("J,%d,%s,%d,%lu,%lu,%lu,%lu,%lu,%lu\n",
               (int)getpid(), g->name, g->threads + 1,
               j_jobs, j_miss, j_late_sum, j_resp_sum, j_resp_min, j_resp_max);
    }
    exit(0);
}

/* ================= 内置策略 ================= */

/* --- 固定 alpha --- */
static int sl_policy_fixed(const sl_window_t *w, void *ud) {
    (void)w; return (int)(usize)ud;
}

/* --- 迟滞爬山(状态即用户手里的 struct) --- */
typedef struct {
    int alpha;
    int step;          /* 探测步长,如 5 */
    int hi_q, lo_q;    /* 死区: max_slowdown 在 [lo,hi]×1000 内不动 */
    int last_j_q;
} sl_hill_t;

static void sl_hill_init(sl_hill_t *h, int alpha0, int step) {
    h->alpha = alpha0; h->step = step;
    h->hi_q = 1500; h->lo_q = 1100; h->last_j_q = -1;
}
static int sl_policy_hill(const sl_window_t *w, void *ud) {
    sl_hill_t *h = (sl_hill_t *)ud;
    int j = w->max_slowdown_q;
    if (j > h->hi_q && h->alpha > 0)        h->alpha -= h->step;
    else if (j < h->lo_q && h->alpha < 100) h->alpha += h->step / 2;
    if (h->alpha < 0) h->alpha = 0;
    if (h->alpha > 100) h->alpha = 100;
    h->last_j_q = j;
    return h->alpha;
}

/* --- SPSA-AdamW(定点,梯度缩放 1024) ---
 * J = max_slowdown_q(越小越好,理想 1000)。
 * 奇偶窗口交替 ±δ 扰动做数值梯度;m/v 平滑并自适应步长;
 * weight decay 把 alpha 拉回 target(证据消失时回归默认策略)。
 *
 * 量纲约定: g 为 dJ/dα ×1024;m/v 同尺度平滑;稳态下
 *   step = lr × 1024 × m / sqrt(v) ≈ lr × 1024(= lr 个 alpha 点),
 *   直接作用于 ×1024 定点的 alpha_f。
 */
typedef struct {
    long long m, v;    /* 动量/二阶矩(梯度 ×1024 尺度) */
    long long t;
    int alpha;         /* 当前值(整数输出) */
    int alpha_f;       /* 内部连续值,×1024 */
    int lr;            /* 稳态步长(alpha 点/窗口),如 8 */
    int target;        /* 衰减目标(默认策略) */
    int delta;         /* SPSA 扰动,如 5 */
    int prev_probe;    /* 上一窗口的扰动方向 ±1 */
    int prev_j_q;
} sl_adamw_t;

static void sl_adamw_init(sl_adamw_t *s, int alpha0, int lr, int target) {
    s->m = s->v = 0; s->t = 0;
    s->alpha = alpha0; s->alpha_f = alpha0 * 1024;
    s->lr = lr; s->target = target; s->delta = 5;
    s->prev_probe = 0; s->prev_j_q = -1;
}

static int sl_policy_adamw(const sl_window_t *w, void *ud) {
    sl_adamw_t *s = (sl_adamw_t *)ud;
    int j = w->max_slowdown_q;

    if (s->prev_probe != 0 && s->prev_j_q >= 0) {
        /* 数值梯度: dJ/dα ≈ (J_now - J_prev) / (探针差 2δ×方向) */
        long long g = (long long)(j - s->prev_j_q) * 1024
                      / (2 * s->delta * s->prev_probe);
        s->t++;
        s->m = s->m * 9 / 10 + g / 10;              /* β1=0.9 */
        s->v = s->v * 99 / 100 + g * g / 100;       /* β2=0.99 */
        /* bias correction 从简(窗口数少时 g 本来就糙);
         * 自适应步长: 信号乱(v 大)步子自动小 */
        long long denom = 1;
        {   /* isqrt(v) */
            long long r = 0, bit = 1LL << 30;
            long long vv = s->v;
            while (bit > vv) bit >>= 2;
            while (bit) {
                if (vv >= r + bit) { vv -= r + bit; r = (r >> 1) + bit; }
                else r >>= 1;
                bit >>= 2;
            }
            denom = r > 1 ? r : 1;
        }
        /* ×1024 定点步长(v1 修复: 不再多除 1024) */
        long long step = (long long)s->lr * 1024 * s->m / denom;
        /* weight decay: λ=0.02 × (target − α),同为 ×1024 尺度 */
        long long decay = (long long)(s->target * 1024 - s->alpha_f) * 2 / 100;
        s->alpha_f -= (int)(step - decay);
        if (s->alpha_f < 0)        s->alpha_f = 0;
        if (s->alpha_f > 102400)   s->alpha_f = 102400;
        s->alpha = (int)(s->alpha_f / 1024);
    }

    /* 下一窗口注入扰动(边界处单向;单向期间梯度估计有偏,从简) */
    if      (s->alpha + s->delta > 100) s->prev_probe = -1;
    else if (s->alpha - s->delta < 0)   s->prev_probe =  1;
    else s->prev_probe = (w->window_no & 1) ? 1 : -1;
    s->prev_j_q = j;

    int probe = s->alpha + s->prev_probe * s->delta;
    if (probe < 0) probe = 0;
    if (probe > 100) probe = 100;
    return probe;
}

/* ================= 监控与运行 ================= */

static int sl_abs_i(int x) { return x < 0 ? -x : x; }

static void sl_measure_window(sl_window_t *w, int win_no, int alpha,
                              unsigned long prev_run[]) {
    int total_eff = 0, total_run = 0;
    w->window_no = win_no; w->alpha = alpha;
    w->nprocs = 0; w->max_slowdown_q = 0;

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
        /* v1: 只有就绪组才参与"应得"分母(睡眠/未启动组不抢 CPU) */
        if (p->ready_threads > 0)
            total_eff += p->eff_tickets > 0 ? p->eff_tickets : 1;
        total_run += (int)p->run_delta;
        w->nprocs++;

        printf("W,%d,%d,%d,%s,%lu,%d,%d\n",
               win_no, alpha, p->pid, p->name,
               p->run_delta, p->eff_tickets, p->ready_threads);
    }

    /* 公平性: 对"实得-应得"偏离求和;最大亏欠度 */
    long long sum = 0, sumsq = 0;
    for (int i = 0; i < w->nprocs; i++) {
        sl_proc_t *p = &w->procs[i];
        if (p->ready_threads <= 0) {   /* 未就绪组不参与评判 */
            p->entitled_q = 0;
            p->share_q = 0;
            p->slowdown_q = 1000;
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

    reset_sched_stat();
    set_sched_alpha(alpha);
    sl_t0 = get_ticks();
    sl_t_end = sl_t0 + cfg->total_ticks;

    /* 拉起负载组(每组一个进程) */
    for (int i = 0; i < sl_ngroups; i++) {
        int pid = (int)fork();
        if (pid == 0) sl_child_main(&sl_groups[i]);
        sl_groups[i].pid = pid;
    }

    printf("# schedlab win=%d total=%lu groups=%d\n",
           sl_window, cfg->total_ticks, sl_ngroups);

    /* 监控循环(注意: S 行打印的是"下一窗口"的 alpha) */
    static unsigned long prev_run[SL_MAX_GROUPS];
    int win = 0;
    while (get_ticks() < sl_t_end) {
        sleep((usize)sl_window);
        win++;
        static sl_window_t w;
        sl_measure_window(&w, win, alpha, prev_run);
        if (cfg->policy) {
            alpha = cfg->policy(&w, cfg->policy_ud);
            if (alpha < 0) alpha = 0;
            if (alpha > 100) alpha = 100;
            set_sched_alpha(alpha);   /* 下个窗口生效,窗口内冻结 */
        }
        printf("S,%d,%d,%d,%d\n", win, alpha, w.jain_q, w.max_slowdown_q);
    }

    /* 收尾(若 waitpid 签名不同,改这里) */
    int code;
    for (int i = 0; i < sl_ngroups; i++) waitpid(sl_groups[i].pid, &code, 0);
    printf("# done\n");
    return 0;
}

#ifdef __cplusplus
}
#endif