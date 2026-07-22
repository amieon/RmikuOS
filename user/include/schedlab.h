/*
 * schedlab.h —— RmikuOS 调度实验框架
 *
 * 覆盖五个实验(CLI 与原 tests/ 对齐,见 schedlab.c):
 *   1. mech   Alpha mechanism test      —— 验证机制(原 35_alpha_arg_test)
 *   2. edge   Edge deadline trade-off   —— 刻画 trade-off(原 36_edge_deadline_arg_test)
 *   3. aimd   Adaptive alpha controller —— AIMD 恒定负载(原 37/39)
 *   4. dyn    Dynamic load experiment   —— AIMD vs 固定 α(原 40)
 *   5. adamw  SPSA-AdamW 自适应         —— 新增对照
 *
 * v2 关键架构变化(ctrl 搬进监控进程):
 *   v1 把所有负载组都 fork 成独立子进程,导致控制器在运行中拿不到
 *   deadline 反馈(J 行是子进程退出才打的)。v2 提供 in-parent jobs 组:
 *   ctrl 线程直接跑在监控进程里,统计走进程内共享计数器(AMO),
 *   控制器零 syscall 读取——与原 37/40 的结构一致,无需新系统调用。
 *
 * 内核依赖(现状已满足,无需新增 syscall):
 *   fork/thread_create/thread_exit/sleep/get_ticks/getpid/exit/waitpid
 *   set_my_tickets/set_sched_alpha/get_process_sched_stat/reset_sched_stat
 *
 * 输出 CSV(全部原始量,推导交给宿主机 Python):
 *   W,win,alpha,pid,name,run_delta,eff_tickets,ready_threads
 *   D,win,alpha,jobs_delta,miss_delta,late_delta      (仅 in-parent jobs 组)
 *   A,win,alpha_before,alpha_after,action             (AIMD 决策轨迹)
 *   J,pid,name,threads,jobs,miss,late_sum,late_max,resp_sum,resp_sumsq,resp_min,resp_max
 *   K,pid,name,threads,work                           (spin 组收尾:吞吐 work)
 *   S,win,next_alpha,jain_q,max_slowdown_q
 *
 * v2 相对 v1.1 的变更:
 *   + in-parent jobs 组(SL_F_IN_PARENT),D/A 行,窗口 deadline 差分进 sl_window_t
 *   + AIMD 策略(逐行移植 40_dynamic_load_exp.c:INC=5,BACKOFF=80,
 *     滞回 0/25 tick,分档 900/500,冷却 1,late-probe 保护 3 窗口)
 *   + AdamW 的 loss 改为 deadline 损失(miss_per_1000 + 平均迟到,与 AIMD 同信号竞技)
 *   + spin 组 work 计数(K 行);J 行补 late_max/resp_sumsq(exp2 jitter 用)
 *   + dyn 相位改为 40 的"活跃子集"方案(轻相位只 light_active 个线程活跃),
 *     移除 v1 的中途加入/退出组
 *   + cfg.start_delay(默认 80 tick,对齐原 START_DELAY_TICKS)
 *   - 移除 hill 策略(占位基线,真实基线是你的 AIMD)
 *   ! SL_MAX_THREADS 64 -> 128(容纳 ai=100 的原配置)
 *
 * 致命教训(v1 已踩,勿忘):本头文件所有文件级可变变量必须零初始化(.bss)!
 *   非零初始化会进 .data,而用户链接脚本把 .data 捆进只读 .text,
 *   写入即 store page fault。任何"默认初值"都在 sl_run/init 函数里赋。
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "user.h"

/* #define printf uprintf */

#define SL_MAX_GROUPS  8
#define SL_MAX_THREADS 128
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

/* 三段相位:0 轻 / 1 重 / 2 轻(锚定 sl_t0..sl_t_end 三等分,复刻 40) */
static int sl_phase_now(void) {
    unsigned long span = sl_t_end - sl_t0;
    unsigned long seg = span / 3;
    unsigned long now = get_ticks();
    unsigned long off = now > sl_t0 ? now - sl_t0 : 0;
    int ph = seg ? (int)(off / seg) : 0;
    if (ph > 2) ph = 2;
    return ph;
}

/* 该线程此刻是否该干活;不该干时睡到下一相位边界。返回 0=干活。
 * idx < light_active 的线程轻相位保底活跃;主线程(idx=-1)按普通超编线程
 * 处理,轻相位也睡——轻负载恰好 = light_active 个线程,不多不少。 */
static long sl_phased_sleep(const sl_group_t *g, int idx) {
    if (!(g->flags & SL_F_PHASED)) return 0;
    if (idx >= 0 && idx < g->light_active) return 0;
    if (sl_phase_now() == 1) return 0;         /* 重相位全员活跃 */
    unsigned long span = sl_t_end - sl_t0;
    unsigned long seg = span / 3;
    unsigned long boundary = sl_t0 + (unsigned long)(sl_phase_now() + 1) * seg;
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
    for (int i = 0; i < g->threads; i++) {
        sl_args[gi][i].g = g;
        sl_args[gi][i].idx = i;
        thread_create(g->kind == SL_JOBS ? sl_job_fn : sl_spin_fn,
                      &sl_args[gi][i]);
    }
    /* 子进程主线程也干活(省一个线程位) */
    if (g->kind == SL_JOBS) sl_job_fn(&(sl_task_arg_t){ g, -1 });
    else                    sl_spin_fn(&(sl_task_arg_t){ g, -1 });
    /* 自报汇总 */
    if (g->kind == SL_JOBS) sl_print_j(gi);
    else printf("K,%d,%s,%d,%lu\n", g->pid, g->name, g->threads + 1,
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
        a->cooldown = 1;
        action = "down";
    } else if ((long)w->late_delta <= a->safe_lateness) {
        a->safe_windows++;
        /* SAFE_WINDOWS_TO_PROBE_UP = 1(40 版) */
        if (!can_probe_up) {
            action = "late_hold";
        } else {
            int na = a->alpha + a->inc;
            if (na > 100) na = 100;
            a->alpha = na;
            action = "up";
        }
        a->safe_windows = 0;
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
 * 注意:ctrl 空闲的窗口 loss=0、梯度≈0,alpha 被 weight decay 拉回 target,
 * 不像 AIMD 会主动爬高——这是两种控制器的本质差异之一,实验 5 的看点。
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
} sl_adamw_t;

static void sl_adamw_init(sl_adamw_t *s, int alpha0, int lr, int target) {
    s->m = s->v = 0; s->t = 0;
    s->alpha = alpha0; s->alpha_f = alpha0 * 1024;
    s->lr = lr; s->target = target; s->delta = 5;
    s->prev_probe = 0; s->prev_loss = -1;
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
        s->alpha_f -= (int)(step - decay);
        if (s->alpha_f < 0)      s->alpha_f = 0;
        if (s->alpha_f > 102400) s->alpha_f = 102400;
        s->alpha = (int)(s->alpha_f / 1024);
    }

    if      (s->alpha + s->delta > 100) s->prev_probe = -1;
    else if (s->alpha - s->delta < 0)   s->prev_probe =  1;
    else s->prev_probe = (w->window_no & 1) ? 1 : -1;
    s->prev_loss = loss;

    int probe = s->alpha + s->prev_probe * s->delta;
    if (probe < 0) probe = 0;
    if (probe > 100) probe = 100;
    return probe;
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
    set_sched_alpha(alpha);
    sl_t0 = get_ticks();
    sl_t_end = sl_t0 + cfg->total_ticks;

    /* 拉起负载组;in-parent jobs 组在监控进程内直接起线程 */
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

    /* 起始对齐:等 start_delay 再进第一个窗口 */
    long d0 = (long)(sl_t0 + start_delay) - (long)get_ticks();
    if (d0 > 0) sleep((usize)d0);

    printf("# schedlab win=%d total=%lu groups=%d\n",
           sl_window, cfg->total_ticks, sl_ngroups);

    static unsigned long prev_run[SL_MAX_GROUPS];
    static unsigned long prev_jobs[SL_MAX_GROUPS];
    static unsigned long prev_miss[SL_MAX_GROUPS];
    static unsigned long prev_late[SL_MAX_GROUPS];
    int win = 0;
    while (get_ticks() < sl_t_end) {
        sleep((usize)sl_window);
        win++;
        static sl_window_t w;
        w.remain_windows = (int)((sl_t_end > get_ticks()
            ? sl_t_end - get_ticks() : 0) / (unsigned long)sl_window);
        sl_measure_window(&w, win, alpha, prev_run, prev_jobs, prev_miss, prev_late);
        if (cfg->policy) {
            alpha = cfg->policy(&w, cfg->policy_ud);
            if (alpha < 0) alpha = 0;
            if (alpha > 100) alpha = 100;
            set_sched_alpha(alpha);
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
    return 0;
}

#ifdef __cplusplus
}
#endif