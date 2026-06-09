#include "user.h"

#define MAX_THREADS 25

#define CONTROL_TICKETS 300
#define AI_TICKETS 100
#define LOGGER_TICKETS 50

#define START_DELAY_TICKS 80
#define TEST_TICKS 600

#define AI_BURN_ITERS 12000
#define LOGGER_BURN_ITERS 12000

#define CONTROL_PERIOD_TICKS 4
#define CONTROL_JOB_CPU_TICKS 2
#define CONTROL_SPIN_BURN_ITERS 400000

#define ADAPT_WINDOW_TICKS 100
#define ALPHA_STEP 25
#define ZERO_MISS_WINDOWS_TO_INCREASE 3

#define ALPHA_STEP 25

/*
 * 连续几个完全安全窗口后，才尝试提高 alpha。
 * 越大越保守。
 */
#define SAFE_WINDOWS_TO_PROBE_UP 2

/*
 * 每个窗口大约 25 个 control jobs。
 * 1 次 miss 大概就是 40 / 1000。
 *
 * >= 50/1000 认为明显不安全；
 * >= 200/1000 认为严重不安全，会快速下降。
 */
#define UNSAFE_MISS_PER_1000 50
#define SEVERE_MISS_PER_1000 200

static volatile usize global_start_tick;
static volatile usize global_end_tick;

static volatile usize control_jobs[MAX_THREADS];
static volatile usize control_miss[MAX_THREADS];

static volatile usize counters[MAX_THREADS];

struct arg {
    int id;
};

static struct arg args[MAX_THREADS];

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
    int max_allowed_alpha,
    int safe_windows,
    usize jobs,
    usize miss,
    const char *action
) {
    char buf[512];
    int pos = 0;

    usize miss_per_1000 = 0;

    if (jobs > 0) {
        miss_per_1000 = miss * 1000 / jobs;
    }

    pos = append_str(buf, pos, "[adaptive_window] window=");
    pos = append_usize(buf, pos, (usize)window_id);

    pos = append_str(buf, pos, " alpha_before=");
    pos = append_usize(buf, pos, (usize)alpha_before);

    pos = append_str(buf, pos, " alpha_after=");
    pos = append_usize(buf, pos, (usize)alpha_after);

    pos = append_str(buf, pos, " max_allowed=");
    pos = append_usize(buf, pos, (usize)max_allowed_alpha);

    pos = append_str(buf, pos, " safe_windows=");
    pos = append_usize(buf, pos, (usize)safe_windows);

    pos = append_str(buf, pos, " jobs=");
    pos = append_usize(buf, pos, jobs);

    pos = append_str(buf, pos, " miss=");
    pos = append_usize(buf, pos, miss);

    pos = append_str(buf, pos, " miss_per_1000=");
    pos = append_usize(buf, pos, miss_per_1000);

    pos = append_str(buf, pos, " action=");
    pos = append_str(buf, pos, action);

    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static void print_result_line(
    int alpha,
    const char *role,
    int threads,
    int tickets,
    usize work,
    usize jobs,
    usize miss
) {
    char buf[384];
    int pos = 0;

    /*
     * effective/ready/run_ticks 这里填 0。
     * 真正的调度采样由父进程在 [edge_sample] 行输出。
     * 这样 child 退出后仍然能保留 work/jobs/miss 结果。
     */
    pos = append_str(buf, pos, "[edge_deadline] alpha=");
    pos = append_usize(buf, pos, (usize)alpha);

    pos = append_str(buf, pos, " role=");
    pos = append_str(buf, pos, role);

    pos = append_str(buf, pos, "_result");

    pos = append_str(buf, pos, " pid=");
    pos = append_usize(buf, pos, (usize)getpid());

    pos = append_str(buf, pos, " threads=");
    pos = append_usize(buf, pos, (usize)threads);

    pos = append_str(buf, pos, " tickets=");
    pos = append_usize(buf, pos, (usize)tickets);

    pos = append_str(buf, pos, " effective=0");

    pos = append_str(buf, pos, " ready=0");

    pos = append_str(buf, pos, " run_ticks=0");

    pos = append_str(buf, pos, " work=");
    pos = append_usize(buf, pos, work);

    pos = append_str(buf, pos, " jobs=");
    pos = append_usize(buf, pos, jobs);

    pos = append_str(buf, pos, " miss=");
    pos = append_usize(buf, pos, miss);

    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static void print_parent_stat_line(
    int alpha,
    const char *role,
    int threads,
    int tickets,
    struct sched_proc_stat *st
) {
    char buf[384];
    int pos = 0;

    pos = append_str(buf, pos, "[edge_sample] alpha=");
    pos = append_usize(buf, pos, (usize)alpha);

    pos = append_str(buf, pos, " role=");
    pos = append_str(buf, pos, role);

    pos = append_str(buf, pos, " pid=");
    pos = append_usize(buf, pos, (usize)st->pid);

    pos = append_str(buf, pos, " threads=");
    pos = append_usize(buf, pos, (usize)threads);

    pos = append_str(buf, pos, " tickets=");
    pos = append_usize(buf, pos, (usize)tickets);

    pos = append_str(buf, pos, " effective=");
    pos = append_usize(buf, pos, (usize)st->effective_tickets);

    pos = append_str(buf, pos, " ready=");
    pos = append_usize(buf, pos, (usize)st->ready_threads);

    pos = append_str(buf, pos, " run_ticks=");
    pos = append_usize(buf, pos, (usize)st->run_ticks);

    pos = append_str(buf, pos, " stride=");
    pos = append_usize(buf, pos, (usize)st->stride);

    pos = append_str(buf, pos, " pass=");
    pos = append_usize(buf, pos, (usize)st->pass);

    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
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

    wait_until(global_start_tick);

    while (get_ticks() < global_end_tick) {
        burn_iters(AI_BURN_ITERS);
        counters[id]++;

        if ((counters[id] & 0x3f) == 0) {
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

    for (int i = 0; i < MAX_THREADS; i++) {
        control_jobs[i] = 0;
        control_miss[i] = 0;
    }

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

    usize total_jobs = 0;
    usize total_miss = 0;

    for (int i = 0; i < control_threads; i++) {
        total_jobs += control_jobs[i];
        total_miss += control_miss[i];
    }

    print_result_line(
        alpha,
        "control",
        control_threads,
        CONTROL_TICKETS,
        0,
        total_jobs,
        total_miss
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
        0
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
        0
    );

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

    for (int i = 0; i < MAX_THREADS; i++) {
        control_jobs[i] = 0;
        control_miss[i] = 0;
    }

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

    usize last_jobs = 0;
    usize last_miss = 0;

    int safe_windows = 0;

    /*
    * max_allowed_alpha 表示本轮实验中还允许尝试的最高 alpha。
    * 一旦某个 alpha 被判定为不安全，就把上界压到它下面，
    * 避免反复试探同一个坏 alpha。
    */
    int max_allowed_alpha = 100;

    int window_id = 0;

    while (get_ticks() + ADAPT_WINDOW_TICKS < global_end_tick) {
        sleep(ADAPT_WINDOW_TICKS);

        usize total_jobs = 0;
        usize total_miss = 0;

        for (int i = 0; i < control_threads; i++) {
            total_jobs += control_jobs[i];
            total_miss += control_miss[i];
        }

        usize window_jobs = total_jobs - last_jobs;
        usize window_miss = total_miss - last_miss;

        last_jobs = total_jobs;
        last_miss = total_miss;

        usize miss_per_1000 = 0;

        if (window_jobs > 0) {
            miss_per_1000 = window_miss * 1000 / window_jobs;
        }

        int alpha_before = alpha;
        const char *action = "hold";

        if (miss_per_1000 >= UNSAFE_MISS_PER_1000) {
            /*
            * 当前 alpha 已经不安全。
            * 以后本轮实验不再尝试这个 alpha 或更高 alpha。
            */
            int new_max = alpha - ALPHA_STEP;

            if (new_max < 0) {
                new_max = 0;
            }

            if (new_max < max_allowed_alpha) {
                max_allowed_alpha = new_max;
            }

            if (miss_per_1000 >= SEVERE_MISS_PER_1000) {
                alpha = clamp_alpha(alpha - 2 * ALPHA_STEP);
                action = "severe_down";
            } else {
                alpha = clamp_alpha(alpha - ALPHA_STEP);
                action = "unsafe_down";
            }

            if (alpha > max_allowed_alpha) {
                alpha = max_allowed_alpha;
            }

            safe_windows = 0;
        } else if (window_miss == 0) {
            /*
            * 完全无 miss，认为当前 alpha 是安全的。
            * 连续安全若干窗口后，尝试向上探索。
            */
            safe_windows++;

            if (safe_windows >= SAFE_WINDOWS_TO_PROBE_UP) {
                int next_alpha = alpha + ALPHA_STEP;

                if (next_alpha <= max_allowed_alpha) {
                    alpha = next_alpha;
                    action = "probe_up";
                } else {
                    action = "safe_at_limit";
                }

                safe_windows = 0;
            } else {
                action = "safe_hold";
            }
        } else {
            /*
            * 有少量 miss，但没超过 unsafe 阈值。
            * 认为处于灰区：不升也不降，避免因为 1 次抖动立刻回退。
            */
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
            max_allowed_alpha,
            safe_windows,
            window_jobs,
            window_miss,
            action
        );

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

    usize final_jobs = 0;
    usize final_miss = 0;

    for (int i = 0; i < control_threads; i++) {
        final_jobs += control_jobs[i];
        final_miss += control_miss[i];
    }

    print_result_line(
        alpha,
        "control",
        control_threads,
        CONTROL_TICKETS,
        0,
        final_jobs,
        final_miss
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
    puts("adaptive_alpha_test start\n");
    puts("usage: adaptive_alpha_test <initial_alpha> <control_threads> <ai_threads> <logger_threads>\n");
    puts("example: adaptive_alpha_test 50 1 14 8\n");

    if (argc != 5) {
        puts("[adaptive_alpha] FAIL: need exactly 4 args\n");
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

    if (!(initial_alpha == 0 ||
          initial_alpha == 25 ||
          initial_alpha == 50 ||
          initial_alpha == 75 ||
          initial_alpha == 100)) {
        puts("[adaptive_alpha] FAIL: alpha must be 0,25,50,75,100\n");
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

    int ret = run_one_adaptive_case(
        initial_alpha,
        control_threads,
        ai_threads,
        logger_threads
    );

    set_sched_alpha(50);

    if (ret != 0) {
        return 1;
    }

    puts("\nadaptive_alpha_test PASS\n");

    return 0;
}