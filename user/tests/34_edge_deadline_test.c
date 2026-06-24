#include "user.h"

#define CONTROL_THREADS 1
#define AI_THREADS 9
#define LOGGER_THREADS 4
#define MAX_THREADS 9

#define CONTROL_TICKETS 300
#define AI_TICKETS 100
#define LOGGER_TICKETS 50

#define START_DELAY_TICKS 80
#define TEST_TICKS 600



#define AI_BURN_ITERS 12000
#define LOGGER_BURN_ITERS 12000

static volatile usize global_start_tick;
static volatile usize global_end_tick;

static volatile usize control_jobs;
static volatile usize control_miss;

static volatile usize counters[MAX_THREADS];

struct arg {
    int id;
};

static struct arg args[MAX_THREADS];


static void burn_iters(int iters) {
    volatile usize x = 0;

    for (int i = 0; i < iters; i++) {
        x = x * 1103515245u + 12345u + (usize)i;
    }

    (void)x;
}


static void print_stat_line(
    int alpha,
    const char *role,
    int threads,
    int tickets,
    usize work,
    usize jobs,
    usize miss,
    struct sched_proc_stat *st
) {
    char buf[384];
    int pos = 0;

    pos = append_str(buf, pos, "[edge_deadline] alpha=");
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

    pos = append_str(buf, pos, " work=");
    pos = append_usize(buf, pos, work);

    pos = append_str(buf, pos, " jobs=");
    pos = append_usize(buf, pos, jobs);

    pos = append_str(buf, pos, " miss=");
    pos = append_usize(buf, pos, miss);

    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static void wait_until(usize tick) {
    while (get_ticks() < tick) {
        yield();
    }
}
static void print_parent_stat_line(
    int alpha,
    const char *role,
    int threads,
    int tickets,
    struct sched_proc_stat *st
) {
    char buf[320];
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
    int pid_logger
) {
    /*
     * 接近结束前采样，比 TEST_TICKS / 2 更接近总运行状态，
     * 同时 worker 还没退出，ready/effective 更可靠。
     */
    usize sample_tick = global_end_tick - 20;

    while (get_ticks() < sample_tick) {
        sleep(1);
    }

    struct sched_proc_stat st;

    if (get_process_sched_stat(pid_control, &st) == 0) {
        print_parent_stat_line(
            alpha,
            "control",
            CONTROL_THREADS,
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
            AI_THREADS,
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
            LOGGER_THREADS,
            LOGGER_TICKETS,
            &st
        );
    } else {
        puts("[edge_sample] FAIL: logger stat\n");
    }
}

/*
 * control 每 CONTROL_PERIOD_TICKS 必须s完成一次 job。
 * 如果 miss 全是 0：调大 CONTROL_JOB_BURN_ITERS 或调小 CONTROL_PERIOD_TICKS。
 * 如果 miss 全很高：调小 CONTROL_JOB_BURN_ITERS 或调大 CONTROL_PERIOD_TICKS。
 */

#define CONTROL_PERIOD_TICKS 4
#define CONTROL_JOB_CPU_TICKS 2
#define CONTROL_SPIN_BURN_ITERS 200000

static void control_worker(void *raw) {
    (void)raw;

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
        control_jobs++;
        if (missed) {
            control_miss++;
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

static int run_control(int alpha) {
    if (set_my_tickets(CONTROL_TICKETS) < 0) {
        puts("[edge_deadline] FAIL: set_my_tickets control\n");
        return 1;
    }

    control_jobs = 0;
    control_miss = 0;

    int tid = thread_create(control_worker, 0);

    if (tid < 0) {
        puts("[edge_deadline] FAIL: control thread_create\n");
        return 1;
    }

    /*
     * 关键：不要 sleep(1) 轮询。
     * 直接 join，让主线程阻塞，不参与 ready/runnable 统计。
     */
    int code = -1;
    int ret = thread_join(tid, &code);

    if (ret != tid || code != 0) {
        puts("[edge_deadline] FAIL: control join\n");
        puts("ret=");
        put_int(ret);
        puts(" code=");
        put_int(code);
        puts("\n");
        return 1;
    }

    struct sched_proc_stat dummy;
    dummy.pid = getpid();
    dummy.effective_tickets = 0;
    dummy.ready_threads = 0;
    dummy.run_ticks = 0;

    print_stat_line(
        alpha,
        "control_result",
        CONTROL_THREADS,
        CONTROL_TICKETS,
        0,
        control_jobs,
        control_miss,
        &dummy
    );

    return 0;
}

static int run_ai(int alpha) {
    if (set_my_tickets(AI_TICKETS) < 0) {
        puts("[edge_deadline] FAIL: set_my_tickets ai\n");
        return 1;
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        counters[i] = 0;
    }

    int tids[AI_THREADS];

    for (int i = 0; i < AI_THREADS; i++) {
        args[i].id = i;

        tids[i] = thread_create(throughput_worker, &args[i]);

        if (tids[i] < 0) {
            puts("[edge_deadline] FAIL: ai thread_create\n");
            return 1;
        }
    }

    usize total = 0;

    /*
     * 直接 join，主线程阻塞，不参与 runnable 统计。
     */
    for (int i = 0; i < AI_THREADS; i++) {
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

    struct sched_proc_stat dummy;
    dummy.pid = getpid();
    dummy.effective_tickets = 0;
    dummy.ready_threads = 0;
    dummy.run_ticks = 0;

    print_stat_line(
        alpha,
        "ai_result",
        AI_THREADS,
        AI_TICKETS,
        total,
        0,
        0,
        &dummy
    );

    return 0;
}

static int run_logger(int alpha) {
    if (set_my_tickets(LOGGER_TICKETS) < 0) {
        puts("[edge_deadline] FAIL: set_my_tickets logger\n");
        return 1;
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        counters[i] = 0;
    }

    int tids[LOGGER_THREADS];

    for (int i = 0; i < LOGGER_THREADS; i++) {
        args[i].id = i;

        tids[i] = thread_create(logger_worker, &args[i]);

        if (tids[i] < 0) {
            puts("[edge_deadline] FAIL: logger thread_create\n");
            return 1;
        }
    }

    usize total = 0;

    /*
     * 直接 join，主线程阻塞。
     */
    for (int i = 0; i < LOGGER_THREADS; i++) {
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

    struct sched_proc_stat dummy;
    dummy.pid = getpid();
    dummy.effective_tickets = 0;
    dummy.ready_threads = 0;
    dummy.run_ticks = 0;

    print_stat_line(
        alpha,
        "logger_result",
        LOGGER_THREADS,
        LOGGER_TICKETS,
        total,
        0,
        0,
        &dummy
    );

    return 0;
}
static int run_one_alpha(int alpha) {
    puts("\n[edge_deadline] run alpha=");
    put_int(alpha);
    puts("\n");

    if (set_sched_alpha(alpha) < 0) {
        puts("FAIL: set_sched_alpha\n");
        return 1;
    }

    if (reset_sched_stat() < 0) {
        puts("FAIL: reset_sched_stat\n");
        return 1;
    }

    usize now = get_ticks();
    global_start_tick = now + START_DELAY_TICKS;
    global_end_tick = global_start_tick + TEST_TICKS;

    int pid_control = fork();

    if (pid_control < 0) {
        puts("FAIL: fork control\n");
        return 1;
    }

    if (pid_control == 0) {
        int code = run_control(alpha);
        exit(code);
    }

    int pid_ai = fork();

    if (pid_ai < 0) {
        puts("FAIL: fork ai\n");
        return 1;
    }

    if (pid_ai == 0) {
        int code = run_ai(alpha);
        exit(code);
    }

    int pid_logger = fork();

    if (pid_logger < 0) {
        puts("FAIL: fork logger\n");
        return 1;
    }
    if (pid_logger == 0) {
        int code = run_logger(alpha);
        exit(code);
    }

    /*
    * 父进程在子进程 workload 还在运行时采样。
    * 这样 ai/logger 的 ready/effective 才能看到 9/4 个线程。
    */
    sample_children_stat(
        alpha,
        pid_control,
        pid_ai,
        pid_logger
    );

    int code_control = -1;
    int code_ai = -1;
    int code_logger = -1;

    int ret_control = waitpid(pid_control, &code_control);
    int ret_ai = waitpid(pid_ai, &code_ai);
    int ret_logger = waitpid(pid_logger, &code_logger);

    if (ret_control != pid_control || code_control != 0) {
        puts("FAIL: control child failed\n");
        puts("ret=");
        put_int(ret_control);
        puts(" code=");
        put_int(code_control);
        puts("\n");
        return 1;
    }

    if (ret_ai != pid_ai || code_ai != 0) {
        puts("FAIL: ai child failed\n");
        puts("ret=");
        put_int(ret_ai);
        puts(" code=");
        put_int(code_ai);
        puts("\n");
        return 1;
    }

    if (ret_logger != pid_logger || code_logger != 0) {
        puts("FAIL: logger child failed\n");
        puts("ret=");
        put_int(ret_logger);
        puts(" code=");
        put_int(code_logger);
        puts("\n");
        return 1;
    }

    return 0;
}

int main(void) {
    puts("edge_deadline_test start\n");
    puts("scenario: edge gateway with control deadline + AI throughput + logger background\n");
    puts("control has periodic deadline; AI/logger are throughput workloads\n");

    if (run_one_alpha(0) != 0) {
        return 1;
    }
    if (run_one_alpha(50) != 0) {
        return 1;
    }

    if (run_one_alpha(100) != 0) {
        return 1;
    }

    set_sched_alpha(50);

    puts("\nedge_deadline_test PASS\n");
    puts("manual check:\n");
    puts("  alpha=0   should protect control but reduce AI throughput\n");
    puts("  alpha=50  should balance control miss and AI throughput\n");
    puts("  alpha=100 should increase AI throughput, possibly increasing control miss\n");

    return 0;
}