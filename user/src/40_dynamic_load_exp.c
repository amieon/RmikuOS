#include "user.h"

#define MAX_THREADS 225

#define CONTROL_TICKETS 300
#define AI_TICKETS 100
#define LOGGER_TICKETS 50

#define START_DELAY_TICKS 80
#define TEST_TICKS 18000


#define AI_BURN_ITERS 12000
#define LOGGER_BURN_ITERS 12000

#define CONTROL_PERIOD_TICKS 4
#define CONTROL_JOB_CPU_TICKS 2
#define CONTROL_SPIN_BURN_ITERS 400000

#define ADAPT_WINDOW_TICKS 100
#define ALPHA_STEP 25

/*
 * 连续几个完全安全窗口后，才尝试提高 alpha。
 */
#define SAFE_WINDOWS_TO_PROBE_UP 1

/*
 * 实验快结束时不再向上探测，避免 late probe damage。
 * 例如最后 1~2 个 window 再试 alpha=100，miss 会计入结果，
 * 但吞吐收益来不及体现。
 */
#define MIN_REMAIN_WINDOWS_TO_PROBE 3

/*
 * 每个窗口大约 25 个 control jobs。
 * 1 次 miss 大约是 40 / 1000。
 *
 * >= 100/1000 认为明显不安全；
 * >= 500/1000 认为严重不安全。
 *
 * 这样 1~2 次偶发 miss 不会立刻把 alpha 打下去。
 */
#define UNSAFE_MISS_PER_1000 100
#define SEVERE_MISS_PER_1000 500

/*
 * 从高 alpha 降下来后，给 control 一个窗口消化 backlog。
 */
#define COOLDOWN_WINDOWS_AFTER_DOWN 1

static volatile usize global_start_tick;
static volatile usize global_end_tick;

static volatile usize control_jobs[MAX_THREADS];
static volatile usize control_miss[MAX_THREADS];

/*
 * 观测性增强：control 每线程的 tardiness / response-time 聚合量。
 * 全部是整数原始量，平均/标准差等推导放到宿主机的 Python 脚本里做。
 *
 *   lateness = max(0, finish - deadline)   迟到量（ticks）
 *   resp     = finish - release            响应时间（ticks，release <= finish）
 *
 * resp_min 初值用 (usize)-1 作哨兵；若整轮没有任何 job，打印时再归零。
 */
static volatile usize control_lateness_sum[MAX_THREADS];
static volatile usize control_lateness_max[MAX_THREADS];
static volatile usize control_resp_sum[MAX_THREADS];
static volatile usize control_resp_sumsq[MAX_THREADS];
static volatile usize control_resp_min[MAX_THREADS];
static volatile usize control_resp_max[MAX_THREADS];

static volatile usize counters[MAX_THREADS];

/* ---- 动态负载（三段：轻 → 重 → 轻）---- */
#define DYN_PHASES 3
/* 轻阶段活跃比例的分子/分母：20% = 1/5 */
#define DYN_LIGHT_NUM 1
#define DYN_LIGHT_DEN 5

/*
 * 返回当前 tick 下，本 AI 线程是否应当活跃。
 * id: 线程编号(0..ai_threads-1)；ai_threads: AI 线程总数。
 *
 * 把 [start, end) 均分成 3 段：轻、重、轻。
 * 轻阶段只让前 ceil(ai_threads * 1/5) 个线程活跃，其余空转 yield。
 * 重阶段全部活跃。
 */
/* 轻阶段：只留固定少量 AI 活跃（真·轻负载），不按比例 */
#define DYN_LIGHT_ACTIVE 3

static int ai_active_now(int id, int ai_threads, usize now) {
    usize span = global_end_tick - global_start_tick;
    usize seg = span / DYN_PHASES;
    if (seg == 0) return 1;

    usize offset = (now > global_start_tick) ? (now - global_start_tick) : 0;
    usize phase = offset / seg;
    if (phase >= DYN_PHASES) phase = DYN_PHASES - 1;

    if (phase == 1) {
        return 1;                /* 重：全部活跃 */
    }
    return id < DYN_LIGHT_ACTIVE; /* 轻：只留 3 个 */
}

struct arg {
    int id;
    int total;
};

static struct arg args[MAX_THREADS];

static void reset_control_stats(void) {
    for (int i = 0; i < MAX_THREADS; i++) {
        control_jobs[i] = 0;
        control_miss[i] = 0;

        control_lateness_sum[i] = 0;
        control_lateness_max[i] = 0;
        control_resp_sum[i]     = 0;
        control_resp_sumsq[i]   = 0;
        control_resp_min[i]     = (usize)-1;   /* 哨兵：最大值 */
        control_resp_max[i]     = 0;
    }
}



/*
 * 把 control_threads 个线程的统计量汇总到一组标量。
 * resp_min 仍可能是哨兵 (usize)-1（线程没跑出 job 时），由调用方/打印处归零。
 */
static void aggregate_control_stats(
    int control_threads,
    usize *out_jobs,
    usize *out_miss,
    usize *out_lateness_sum,
    usize *out_lateness_max,
    usize *out_resp_sum,
    usize *out_resp_sumsq,
    usize *out_resp_min,
    usize *out_resp_max
) {
    usize jobs = 0;
    usize miss = 0;
    usize ls = 0;
    usize lm = 0;
    usize rs = 0;
    usize rsq = 0;
    usize rmin = (usize)-1;
    usize rmax = 0;

    for (int i = 0; i < control_threads; i++) {
        jobs += control_jobs[i];
        miss += control_miss[i];

        ls  += control_lateness_sum[i];
        rs  += control_resp_sum[i];
        rsq += control_resp_sumsq[i];

        if (control_lateness_max[i] > lm) {
            lm = control_lateness_max[i];
        }
        if (control_resp_max[i] > rmax) {
            rmax = control_resp_max[i];
        }
        if (control_resp_min[i] < rmin) {
            rmin = control_resp_min[i];
        }
    }

    *out_jobs = jobs;
    *out_miss = miss;
    *out_lateness_sum = ls;
    *out_lateness_max = lm;
    *out_resp_sum = rs;
    *out_resp_sumsq = rsq;
    *out_resp_min = rmin;
    *out_resp_max = rmax;
}

static int clamp_alpha(int alpha) {
    if (alpha < 0) {
        return 0;
    }

    if (alpha > 100) {
        return 100;
    }

    return alpha;
}

static void burn_iters(int iters) {
    volatile usize x = 0;

    for (int i = 0; i < iters; i++) {
        x = x * 1103515245u + 12345u + (usize)i;
    }

    (void)x;
}

static int parse_int_ptr(const char *s, int *out) {
    int sign = 1;
    int val = 0;

    if (s == 0 || *s == 0) {
        return -1;
    }

    if (*s == '-') {
        sign = -1;
        s++;
    }

    if (*s == 0) {
        return -1;
    }

    while (*s) {
        if (*s < '0' || *s > '9') {
            return -1;
        }

        val = val * 10 + (*s - '0');
        s++;
    }

    *out = val * sign;
    return 0;
}

static void wait_until(usize tick) {
    while (get_ticks() < tick) {
        yield();
    }
}

static void print_adaptive_window(
    int window_id,
    int alpha_before,
    int alpha_after,
    int safe_windows,
    usize jobs,
    usize miss,
    const char *action
) {
    usize miss_per_1000 = 0;
    if (jobs > 0) {
        miss_per_1000 = miss * 1000 / jobs;
    }

    uprintf("[adaptive_window] window=%lu alpha_before=%lu alpha_after=%lu safe_windows=%lu jobs=%lu miss=%lu miss_per_1000=%lu action=%s\n",
        (usize)window_id,
        (usize)alpha_before,
        (usize)alpha_after,
        (usize)safe_windows,
        (usize)jobs,
        (usize)miss,
        (usize)miss_per_1000,
        action);
}

/*
 * is_control 非 0 时，在行尾追加 tardiness / response 统计字段：
 *   lateness_sum lateness_max resp_sum resp_sumsq resp_min resp_max
 * ai / logger 传 is_control=0，行格式保持原样（后六个参数忽略）。
 *
 * 字段顺序必须与宿主机 plot_adaptive_alpha_log.py 的 EDGE_EXTRA_RE 对齐。
 */
static void print_result_line(
    int alpha,
    const char *role,
    int threads,
    int tickets,
    usize work,
    usize jobs,
    usize miss,
    int is_control,
    usize lateness_sum,
    usize lateness_max,
    usize resp_sum,
    usize resp_sumsq,
    usize resp_min,
    usize resp_max
) {
    /* 公共前半段：三种 role 完全一致。注意这里行尾不带 '\n'。 */
    uprintf("[edge_deadline] alpha=%lu role=%s_result pid=%lu threads=%lu tickets=%lu effective=0 ready=0 run_ticks=0 work=%lu jobs=%lu miss=%lu",
        (usize)alpha,
        role,
        (usize)getpid(),
        (usize)threads,
        (usize)tickets,
        work,
        jobs,
        miss);

    if (is_control) {
        /* 整轮没有任何 job 时 resp_min 仍是哨兵，归零避免打印 2^64-1。 */
        if (resp_min == (usize)-1) {
            resp_min = 0;
        }

        uprintf(" lateness_sum=%lu lateness_max=%lu resp_sum=%lu resp_sumsq=%lu resp_min=%lu resp_max=%lu",
            lateness_sum,
            lateness_max,
            resp_sum,
            resp_sumsq,
            resp_min,
            resp_max);
    }

    uprintf("\n");
}

static void print_parent_stat_line(
    int alpha,
    const char *role,
    int threads,
    int tickets,
    struct sched_proc_stat *st
) {
    uprintf("[edge_sample] alpha=%lu role=%s pid=%lu threads=%lu tickets=%lu effective=%lu ready=%lu run_ticks=%lu stride=%lu pass=%lu\n",
        (usize)alpha,
        role,
        (usize)st->pid,
        (usize)threads,
        (usize)tickets,
        (usize)st->effective_tickets,
        (usize)st->ready_threads,
        (usize)st->run_ticks,
        (usize)st->stride,
        (usize)st->pass
    );
}

static void sample_children_stat(
    int alpha,
    int pid_control,
    int pid_ai,
    int pid_logger,
    int control_threads,
    int ai_threads,
    int logger_threads
) {
    usize sample_tick = global_end_tick - 20;

    while (get_ticks() < sample_tick) {
        sleep(1);
    }

    struct sched_proc_stat st;

    if (get_process_sched_stat(pid_control, &st) == 0) {
        print_parent_stat_line(
            alpha,
            "control",
            control_threads,
            CONTROL_TICKETS,
            &st
        );
    } else {
        puts("[edge_sample] FAIL: control stat\n");
    }

    if (get_process_sched_stat(pid_ai, &st) == 0) {
        print_parent_stat_line(
            alpha,
            "ai",
            ai_threads,
            AI_TICKETS,
            &st
        );
    } else {
        puts("[edge_sample] FAIL: ai stat\n");
    }

    if (get_process_sched_stat(pid_logger, &st) == 0) {
        print_parent_stat_line(
            alpha,
            "logger",
            logger_threads,
            LOGGER_TICKETS,
            &st
        );
    } else {
        puts("[edge_sample] FAIL: logger stat\n");
    }
}

static void control_worker(void *raw) {
    struct arg *a = (struct arg *)raw;
    int id = a->id;

    wait_until(global_start_tick);

    usize release = global_start_tick;
    usize deadline = release + CONTROL_PERIOD_TICKS;

    while (release < global_end_tick) {
        while (get_ticks() < release) {
            yield();
        }

        if (get_ticks() >= global_end_tick) {
            break;
        }

        int missed = 0;

        if (get_ticks() > deadline) {
            missed = 1;
        }

        usize job_start = get_ticks();
        usize job_target = job_start + CONTROL_JOB_CPU_TICKS;

        while (get_ticks() < job_target) {
            burn_iters(CONTROL_SPIN_BURN_ITERS);
        }

        usize finish = get_ticks();

        if (finish > deadline) {
            missed = 1;
        }

        /* ---- 观测性增强：tardiness / response 统计 ---- */
        usize resp = finish - release;                 /* release 一定 <= finish */
        usize late = (finish > deadline) ? (finish - deadline) : 0;

        control_resp_sum[id]   += resp;
        control_resp_sumsq[id] += resp * resp;

        if (resp < control_resp_min[id]) {
            control_resp_min[id] = resp;
        }
        if (resp > control_resp_max[id]) {
            control_resp_max[id] = resp;
        }

        control_lateness_sum[id] += late;
        if (late > control_lateness_max[id]) {
            control_lateness_max[id] = late;
        }
        /* ---- 增强结束 ---- */

        control_jobs[id]++;

        if (missed) {
            control_miss[id]++;
        }

        release += CONTROL_PERIOD_TICKS;
        deadline += CONTROL_PERIOD_TICKS;

        yield();
    }

    thread_exit(0);
}

static void throughput_worker(void *raw) {
    struct arg *a = (struct arg *)raw;
    int id = a->id;
    int ai_threads = a->total;   /* 见下方 struct arg 改动 */

    wait_until(global_start_tick);

    while (get_ticks() < global_end_tick) {
        if (ai_active_now(id, ai_threads, get_ticks())) {
            burn_iters(AI_BURN_ITERS);
            counters[id]++;

            if ((counters[id] & 0x3f) == 0) {
                yield();
            }
        } else {
            /* 本阶段本线程不活跃：让出 CPU，不产生 work */
            yield();
        }
    }

    thread_exit(0);
}

static void logger_worker(void *raw) {
    struct arg *a = (struct arg *)raw;
    int id = a->id;

    wait_until(global_start_tick);

    while (get_ticks() < global_end_tick) {
        burn_iters(LOGGER_BURN_ITERS);
        counters[id]++;

        if ((counters[id] & 0x3f) == 0) {
            yield();
        }
    }

    thread_exit(0);
}

static int run_control(int alpha, int control_threads) {
    if (set_my_tickets(CONTROL_TICKETS) < 0) {
        puts("[edge_deadline] FAIL: set_my_tickets control\n");
        return 1;
    }

    reset_control_stats();

    int tids[MAX_THREADS];

    for (int i = 0; i < control_threads; i++) {
        args[i].id = i;

        tids[i] = thread_create(control_worker, &args[i]);

        if (tids[i] < 0) {
            puts("[edge_deadline] FAIL: control thread_create\n");
            return 1;
        }
    }

    for (int i = 0; i < control_threads; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        if (ret != tids[i] || code != 0) {
            puts("[edge_deadline] FAIL: control join\n");
            puts("i=");
            put_int(i);
            puts(" tid=");
            put_int(tids[i]);
            puts(" ret=");
            put_int(ret);
            puts(" code=");
            put_int(code);
            puts("\n");
            return 1;
        }
    }

    usize total_jobs, total_miss;
    usize ls, lm, rs, rsq, rmin, rmax;

    aggregate_control_stats(
        control_threads,
        &total_jobs, &total_miss,
        &ls, &lm, &rs, &rsq, &rmin, &rmax
    );

    print_result_line(
        alpha,
        "control",
        control_threads,
        CONTROL_TICKETS,
        0,
        total_jobs,
        total_miss,
        1,
        ls, lm, rs, rsq, rmin, rmax
    );

    return 0;
}

static int run_ai(int alpha, int ai_threads) {
    if (set_my_tickets(AI_TICKETS) < 0) {
        puts("[edge_deadline] FAIL: set_my_tickets ai\n");
        return 1;
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        counters[i] = 0;
    }

    int tids[MAX_THREADS];

    for (int i = 0; i < ai_threads; i++) {
        args[i].id = i;
        args[i].total = ai_threads;
        tids[i] = thread_create(throughput_worker, &args[i]);

        if (tids[i] < 0) {
            puts("[edge_deadline] FAIL: ai thread_create\n");
            return 1;
        }
    }

    usize total = 0;

    for (int i = 0; i < ai_threads; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        if (ret != tids[i] || code != 0) {
            puts("[edge_deadline] FAIL: ai join\n");
            puts("i=");
            put_int(i);
            puts(" tid=");
            put_int(tids[i]);
            puts(" ret=");
            put_int(ret);
            puts(" code=");
            put_int(code);
            puts("\n");
            return 1;
        }

        total += counters[i];
    }

    print_result_line(
        alpha,
        "ai",
        ai_threads,
        AI_TICKETS,
        total,
        0,
        0,
        0,
        0, 0, 0, 0, 0, 0
    );

    return 0;
}

static int run_logger(int alpha, int logger_threads) {
    if (set_my_tickets(LOGGER_TICKETS) < 0) {
        puts("[edge_deadline] FAIL: set_my_tickets logger\n");
        return 1;
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        counters[i] = 0;
    }

    int tids[MAX_THREADS];

    for (int i = 0; i < logger_threads; i++) {
        args[i].id = i;

        tids[i] = thread_create(logger_worker, &args[i]);

        if (tids[i] < 0) {
            puts("[edge_deadline] FAIL: logger thread_create\n");
            return 1;
        }
    }

    usize total = 0;

    for (int i = 0; i < logger_threads; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        if (ret != tids[i] || code != 0) {
            puts("[edge_deadline] FAIL: logger join\n");
            puts("i=");
            put_int(i);
            puts(" tid=");
            put_int(tids[i]);
            puts(" ret=");
            put_int(ret);
            puts(" code=");
            put_int(code);
            puts("\n");
            return 1;
        }

        total += counters[i];
    }

    print_result_line(
        alpha,
        "logger",
        logger_threads,
        LOGGER_TICKETS,
        total,
        0,
        0,
        0,
        0, 0, 0, 0, 0, 0
    );

    return 0;
}
static int run_one_fixed_case(
    int fixed_alpha,
    int control_threads,
    int ai_threads,
    int logger_threads
) {
    puts("\n[adaptive_alpha] run initial_alpha=");
    put_int(fixed_alpha);
    puts(" control_threads=");
    put_int(control_threads);
    puts(" ai_threads=");
    put_int(ai_threads);
    puts(" logger_threads=");
    put_int(logger_threads);
    puts("\n");

    int alpha = fixed_alpha;

    if (set_sched_alpha(alpha) < 0) {
        puts("[adaptive_alpha] FAIL: set_sched_alpha\n");
        return 1;
    }
    if (reset_sched_stat() < 0) {
        puts("[adaptive_alpha] FAIL: reset_sched_stat\n");
        return 1;
    }
    if (set_my_tickets(CONTROL_TICKETS) < 0) {
        puts("[adaptive_alpha] FAIL: set_my_tickets control\n");
        return 1;
    }

    reset_control_stats();

    usize now = get_ticks();
    global_start_tick = now + START_DELAY_TICKS;
    global_end_tick = global_start_tick + TEST_TICKS;

    int pid_ai = fork();
    if (pid_ai < 0) { puts("[adaptive_alpha] FAIL: fork ai\n"); return 1; }
    if (pid_ai == 0) {
        int code = run_ai(fixed_alpha, ai_threads);
        exit(code);
    }

    int pid_logger = fork();
    if (pid_logger < 0) { puts("[adaptive_alpha] FAIL: fork logger\n"); return 1; }
    if (pid_logger == 0) {
        int code = run_logger(fixed_alpha, logger_threads);
        exit(code);
    }

    int tids[MAX_THREADS];
    for (int i = 0; i < control_threads; i++) {
        args[i].id = i;
        tids[i] = thread_create(control_worker, &args[i]);
        if (tids[i] < 0) {
            puts("[adaptive_alpha] FAIL: control thread_create\n");
            return 1;
        }
    }

    wait_until(global_start_tick);

    usize last_jobs = 0;
    usize last_miss = 0;
    usize last_lateness_sum = 0;
    int window_id = 0;

    /*
     * 固定模式：保留与 AIMD 版完全相同的窗口循环结构（同样的 sleep 节奏、
     * 同样的差分采集、同样的逐窗口打印），但循环体内不做任何决策，
     * alpha 全程不变。这样 baseline 与 AIMD 的唯一差别就是“alpha 动不动”，
     * 对照公平。
     */
    while (get_ticks() + ADAPT_WINDOW_TICKS < global_end_tick) {
        sleep(ADAPT_WINDOW_TICKS);

        usize total_jobs = 0, total_miss = 0, total_lateness = 0;
        for (int i = 0; i < control_threads; i++) {
            total_jobs     += control_jobs[i];
            total_miss     += control_miss[i];
            total_lateness += control_lateness_sum[i];
        }

        usize window_jobs = total_jobs - last_jobs;
        usize window_miss = total_miss - last_miss;
        /* window_lateness 不用于决策，但保留差分以维持与 AIMD 版一致的计算量 */
        (void)(total_lateness - last_lateness_sum);

        last_jobs = total_jobs;
        last_miss = total_miss;
        last_lateness_sum = total_lateness;

        print_adaptive_window(
            window_id,
            alpha,          /* alpha_before == alpha_after，固定不变 */
            alpha,
            0,              /* safe_windows 固定模式无意义，填 0 */
            window_jobs,
            window_miss,
            "fixed_hold"
        );
        /* 记录本窗口所处的负载阶段，供分析脚本叠加竖线 */
        usize span = global_end_tick - global_start_tick;
        usize seg = span / DYN_PHASES;
        usize off = (get_ticks() > global_start_tick) ? (get_ticks() - global_start_tick) : 0;
        usize phase = (seg > 0) ? (off / seg) : 0;
        if (phase >= DYN_PHASES) phase = DYN_PHASES - 1;

        uprintf("[load_phase] window=%lu phase=%lu\n",
                (usize)window_id, (usize)phase);
        window_id++;
    }

    /* 采样与最终统计：和 AIMD 版完全一致 */
    struct sched_proc_stat st;
    if (get_process_sched_stat(getpid(), &st) == 0) {
        print_parent_stat_line(alpha, "control", control_threads, CONTROL_TICKETS, &st);
    }
    if (get_process_sched_stat(pid_ai, &st) == 0) {
        print_parent_stat_line(alpha, "ai", ai_threads, AI_TICKETS, &st);
    }
    if (get_process_sched_stat(pid_logger, &st) == 0) {
        print_parent_stat_line(alpha, "logger", logger_threads, LOGGER_TICKETS, &st);
    }

    for (int i = 0; i < control_threads; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);
        if (ret != tids[i] || code != 0) {
            puts("[adaptive_alpha] FAIL: control join\n");
            return 1;
        }
    }

    usize final_jobs, final_miss;
    usize ls, lm, rs, rsq, rmin, rmax;
    aggregate_control_stats(control_threads,
        &final_jobs, &final_miss, &ls, &lm, &rs, &rsq, &rmin, &rmax);

    print_result_line(alpha, "control", control_threads, CONTROL_TICKETS,
        0, final_jobs, final_miss, 1, ls, lm, rs, rsq, rmin, rmax);

    int code_ai = -1, code_logger = -1;
    int ret_ai = waitpid(pid_ai, &code_ai);
    int ret_logger = waitpid(pid_logger, &code_logger);
    if (ret_ai != pid_ai || code_ai != 0) {
        puts("[adaptive_alpha] FAIL: ai child failed\n"); return 1;
    }
    if (ret_logger != pid_logger || code_logger != 0) {
        puts("[adaptive_alpha] FAIL: logger child failed\n"); return 1;
    }

    puts("[adaptive_alpha] final_alpha=");
    put_int(alpha);
    puts("\n");

    return 0;
}

static int run_one_adaptive_case(
    int initial_alpha,
    int control_threads,
    int ai_threads,
    int logger_threads
) {
    puts("\n[adaptive_alpha] run initial_alpha=");
    put_int(initial_alpha);

    puts(" control_threads=");
    put_int(control_threads);

    puts(" ai_threads=");
    put_int(ai_threads);

    puts(" logger_threads=");
    put_int(logger_threads);

    puts("\n");

    int alpha = initial_alpha;

    if (set_sched_alpha(alpha) < 0) {
        puts("[adaptive_alpha] FAIL: set_sched_alpha\n");
        return 1;
    }

    if (reset_sched_stat() < 0) {
        puts("[adaptive_alpha] FAIL: reset_sched_stat\n");
        return 1;
    }

    if (set_my_tickets(CONTROL_TICKETS) < 0) {
        puts("[adaptive_alpha] FAIL: set_my_tickets control\n");
        return 1;
    }

    reset_control_stats();

    usize now = get_ticks();
    global_start_tick = now + START_DELAY_TICKS;
    global_end_tick = global_start_tick + TEST_TICKS;

    int pid_ai = fork();

    if (pid_ai < 0) {
        puts("[adaptive_alpha] FAIL: fork ai\n");
        return 1;
    }

    if (pid_ai == 0) {
        int code = run_ai(initial_alpha, ai_threads);
        exit(code);
    }

    int pid_logger = fork();

    if (pid_logger < 0) {
        puts("[adaptive_alpha] FAIL: fork logger\n");
        return 1;
    }

    if (pid_logger == 0) {
        int code = run_logger(initial_alpha, logger_threads);
        exit(code);
    }

    int tids[MAX_THREADS];

    for (int i = 0; i < control_threads; i++) {
        args[i].id = i;

        tids[i] = thread_create(control_worker, &args[i]);

        if (tids[i] < 0) {
            puts("[adaptive_alpha] FAIL: control thread_create\n");
            return 1;
        }
    }

    wait_until(global_start_tick);

    usize last_lateness_sum = 0;

    /* AIMD 参数（起步值， max_tard 4~7 标定，之后在负载上拧） */
    const int   AIMD_INC      = 5;    /* 加性增：安全时 alpha += 3 */
    const int   AIMD_BACKOFF  = 80;   /* 乘性减：危险时 alpha = alpha*80/100 */
    /* 滞回带：本窗口迟到总量 <= SAFE 算安全，>= DANGER 算危险，中间灰区不动 */
    const usize SAFE_LATENESS   = 0;  /* 一点没迟 = 安全，可上探 */
    const usize DANGER_LATENESS = 25;  /* 本窗口累计迟到 >=15 tick = 危险，回退 */

    usize last_jobs = 0;
    usize last_miss = 0;

    int safe_windows = 0;

    /*
    * max_allowed_alpha 表示本轮实验中还允许尝试的最高 alpha。
    * 一旦某个 alpha 被判定为不安全，就把上界压到它下面，
    * 避免反复试探同一个坏 alpha。
    */
    int max_allowed_alpha = 100;
    int cooldown_windows = 0;

    int window_id = 0;

    while (get_ticks() + ADAPT_WINDOW_TICKS < global_end_tick) {
        sleep(ADAPT_WINDOW_TICKS);

        /* ---- 差分采集本窗口指标 ---- */
        usize total_jobs = 0, total_miss = 0, total_lateness = 0;
        for (int i = 0; i < control_threads; i++) {
            total_jobs     += control_jobs[i];
            total_miss     += control_miss[i];
            total_lateness += control_lateness_sum[i];
        }

        usize window_jobs     = total_jobs     - last_jobs;
        usize window_miss     = total_miss     - last_miss;
        usize window_lateness = total_lateness - last_lateness_sum;

        last_jobs         = total_jobs;
        last_miss         = total_miss;
        last_lateness_sum = total_lateness;

        usize miss_per_1000 = 0;
        if (window_jobs > 0) {
            miss_per_1000 = window_miss * 1000 / window_jobs;
        }

        int alpha_before = alpha;
        const char *action = "hold";

        /* late-probe 保护：快结束了不再上探（沿用你的设计） */
        usize now_tick = get_ticks();
        usize remain_ticks = (global_end_tick > now_tick)
                             ? (global_end_tick - now_tick) : 0;
        int can_probe_up =
            remain_ticks > (usize)(MIN_REMAIN_WINDOWS_TO_PROBE * ADAPT_WINDOW_TICKS);

        /* ---- AIMD 决策核心 ---- */
        if (cooldown_windows > 0) {
            /* 刚乘性减完，给 control 一窗口消化 backlog，只观察 */
            cooldown_windows--;
            safe_windows = 0;
            action = "cooldown_hold";
        } else if (window_lateness >= DANGER_LATENESS) {
            int new_alpha;

            if (miss_per_1000 >= 900) {
                /* 几乎全崩（如突变瞬间 1000/1000）：一步砍到底，别磨蹭 */
                new_alpha = alpha * 40 / 100;        /* ×0.4，暴力逃逸 */
            } else if (miss_per_1000 >= SEVERE_MISS_PER_1000) {  /* >=500 */
                new_alpha = alpha * 60 / 100;        /* ×0.6 */
            } else {
                new_alpha = alpha * AIMD_BACKOFF / 100;  /* ×0.8，原温柔档 */
            }

            if (new_alpha >= alpha) {
                new_alpha = alpha - 1;   /* 保证严格下降 */
            }
            if (new_alpha < 0) {
                new_alpha = 0;
            }
            alpha = new_alpha;
            safe_windows = 0;
            cooldown_windows = COOLDOWN_WINDOWS_AFTER_DOWN;
            action = "aimd_backoff";
        } else if (window_lateness <= SAFE_LATENESS) {
            /* 安全：连续安全若干窗口后，加性增 */
            safe_windows++;
            if (safe_windows >= SAFE_WINDOWS_TO_PROBE_UP) {
                if (!can_probe_up) {
                    action = "late_safe_hold";
                } else {
                    int new_alpha = alpha + AIMD_INC;
                    if (new_alpha > 100) {
                        new_alpha = 100;
                    }
                    alpha = new_alpha;
                    action = "aimd_increase";
                }
                safe_windows = 0;
            } else {
                action = "safe_hold";
            }
        } else {
            /* 灰区（迟到量在 SAFE 和 DANGER 之间）：滞回，不动 */
            safe_windows = 0;
            action = "gray_hold";
        }

        if (alpha != alpha_before) {
            if (set_sched_alpha(alpha) < 0) {
                puts("[adaptive_alpha] FAIL: set_sched_alpha in window\n");
                return 1;
            }
        }

        print_adaptive_window(
            window_id,
            alpha_before,
            alpha,
            safe_windows,
            window_jobs,
            window_miss,
            action
        );
        /* 记录本窗口所处的负载阶段，供分析脚本叠加竖线 */
        usize span = global_end_tick - global_start_tick;
        usize seg = span / DYN_PHASES;
        usize off = (get_ticks() > global_start_tick) ? (get_ticks() - global_start_tick) : 0;
        usize phase = (seg > 0) ? (off / seg) : 0;
        if (phase >= DYN_PHASES) phase = DYN_PHASES - 1;

        uprintf("[load_phase] window=%lu phase=%lu\n",
                (usize)window_id, (usize)phase);
        window_id++;
    }

    /*
     * 接近结束时采样三类进程的调度状态。
     * control 就是当前进程。
     */
    struct sched_proc_stat st;

    if (get_process_sched_stat(getpid(), &st) == 0) {
        print_parent_stat_line(
            alpha,
            "control",
            control_threads,
            CONTROL_TICKETS,
            &st
        );
    }

    if (get_process_sched_stat(pid_ai, &st) == 0) {
        print_parent_stat_line(
            alpha,
            "ai",
            ai_threads,
            AI_TICKETS,
            &st
        );
    }

    if (get_process_sched_stat(pid_logger, &st) == 0) {
        print_parent_stat_line(
            alpha,
            "logger",
            logger_threads,
            LOGGER_TICKETS,
            &st
        );
    }

    for (int i = 0; i < control_threads; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        if (ret != tids[i] || code != 0) {
            puts("[adaptive_alpha] FAIL: control join\n");
            return 1;
        }
    }

    usize final_jobs, final_miss;
    usize ls, lm, rs, rsq, rmin, rmax;

    aggregate_control_stats(
        control_threads,
        &final_jobs, &final_miss,
        &ls, &lm, &rs, &rsq, &rmin, &rmax
    );

    print_result_line(
        alpha,
        "control",
        control_threads,
        CONTROL_TICKETS,
        0,
        final_jobs,
        final_miss,
        1,
        ls, lm, rs, rsq, rmin, rmax
    );

    int code_ai = -1;
    int code_logger = -1;

    int ret_ai = waitpid(pid_ai, &code_ai);
    int ret_logger = waitpid(pid_logger, &code_logger);

    if (ret_ai != pid_ai || code_ai != 0) {
        puts("[adaptive_alpha] FAIL: ai child failed\n");
        return 1;
    }

    if (ret_logger != pid_logger || code_logger != 0) {
        puts("[adaptive_alpha] FAIL: logger child failed\n");
        return 1;
    }

    puts("[adaptive_alpha] final_alpha=");
    put_int(alpha);
    puts("\n");

    return 0;
}

int main(int argc, char **argv) {
    puts("dynamic_load_exp start\n");
    puts("usage: dynamic_load_exp <initial_alpha> <control_threads> <ai_threads> <logger_threads> [mode]\n");
    puts("  mode = adaptive (default) | fixed\n");
    puts("example: dynamic_load_exp 50 1 14 8\n");
    puts("example: dynamic_load_exp 50 1 14 8 fixed\n");

    /* 4 个参数 = 默认 adaptive；5 个参数 = 末位指定模式 */
    if (argc != 5 && argc != 6) {
        puts("[adaptive_alpha] FAIL: need 4 args (+optional mode)\n");
        return 1;
    }

    int initial_alpha = 0;
    int control_threads = 0;
    int ai_threads = 0;
    int logger_threads = 0;

    if (parse_int_ptr(argv[1], &initial_alpha) < 0) {
        puts("[adaptive_alpha] FAIL: bad initial_alpha\n");
        return 1;
    }

    if (parse_int_ptr(argv[2], &control_threads) < 0 ||
        parse_int_ptr(argv[3], &ai_threads) < 0 ||
        parse_int_ptr(argv[4], &logger_threads) < 0) {
        puts("[adaptive_alpha] FAIL: bad thread args\n");
        return 1;
    }

    if (control_threads <= 0 || control_threads > MAX_THREADS ||
        ai_threads <= 0 || ai_threads > MAX_THREADS ||
        logger_threads <= 0 || logger_threads > MAX_THREADS) {
        puts("[adaptive_alpha] FAIL: thread count out of range\n");
        return 1;
    }

    /* 解析模式：默认 adaptive */
    int fixed_mode = 0;

    if (argc == 6) {
        const char *m = argv[5];

        if (str_eq(m, "fixed")) {
            fixed_mode = 1;
        } else if (str_eq(m, "adaptive")) {
            fixed_mode = 0;
        } else {
            puts("[adaptive_alpha] FAIL: mode must be 'adaptive' or 'fixed'\n");
            return 1;
        }
    }

    int ret;

    if (fixed_mode) {
        /* fixed 模式下 initial_alpha 就是全程钉死的 alpha，
           允许任意 0..100，便于扫 baseline 各点 */
        if (initial_alpha < 0 || initial_alpha > 100) {
            puts("[adaptive_alpha] FAIL: fixed alpha out of [0,100]\n");
            return 1;
        }
        ret = run_one_fixed_case(
            initial_alpha,
            control_threads,
            ai_threads,
            logger_threads
        );
    } else {
        ret = run_one_adaptive_case(
            initial_alpha,
            control_threads,
            ai_threads,
            logger_threads
        );
    }

    set_sched_alpha(50);

    if (ret != 0) {
        return 1;
    }

    puts("\ndynamic_load_exp PASS\n");

    return 0;
}