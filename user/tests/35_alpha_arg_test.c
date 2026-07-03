#include "user.h"

#define MAX_PROCS 6
#define MAX_THREADS 50

#define DEFAULT_TICKETS 100

#define START_DELAY_TICKS 80
#define TEST_TICKS 600

#define BURN_ITERS 12000

static volatile usize global_start_tick;
static volatile usize global_end_tick;

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


static void wait_until(usize tick) {
    while (get_ticks() < tick) {
        yield();
    }
}

static void worker(void *raw) {
    struct arg *a = (struct arg *)raw;
    int id = a->id;

    wait_until(global_start_tick);

    while (get_ticks() < global_end_tick) {
        burn_iters(BURN_ITERS);
        counters[id]++;

        if ((counters[id] & 0x3f) == 0) {
            yield();
        }
    }

    thread_exit(0);
}

static void print_sample_line(
    int alpha,
    int role,
    int threads,
    int tickets,
    struct sched_proc_stat *st
) {
    char buf[384];
    int pos = 0;

    pos = append_str(buf, pos, "[alpha_sample] alpha=");
    pos = append_usize(buf, pos, (usize)alpha);

    pos = append_str(buf, pos, " role=");
    pos = append_usize(buf, pos, (usize)role);

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

static void print_result_line(
    int alpha,
    int role,
    int threads,
    int tickets,
    usize work
) {
    char buf[256];
    int pos = 0;

    pos = append_str(buf, pos, "[alpha_result] alpha=");
    pos = append_usize(buf, pos, (usize)alpha);

    pos = append_str(buf, pos, " role=");
    pos = append_usize(buf, pos, (usize)role);

    pos = append_str(buf, pos, " pid=");
    pos = append_usize(buf, pos, (usize)getpid());

    pos = append_str(buf, pos, " threads=");
    pos = append_usize(buf, pos, (usize)threads);

    pos = append_str(buf, pos, " tickets=");
    pos = append_usize(buf, pos, (usize)tickets);

    pos = append_str(buf, pos, " work=");
    pos = append_usize(buf, pos, work);

    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static int run_workload_process(int alpha, int role, int threads) {
    if (set_my_tickets(DEFAULT_TICKETS) < 0) {
        puts("[alpha_arg] FAIL: set_my_tickets\n");
        return 1;
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        counters[i] = 0;
    }

    int tids[MAX_THREADS];

    for (int i = 0; i < threads; i++) {
        args[i].id = i;

        tids[i] = thread_create(worker, &args[i]);

        if (tids[i] < 0) {
            puts("[alpha_arg] FAIL: thread_create\n");
            return 1;
        }
    }

    usize total = 0;

    /*
     * 主线程 join 阻塞，不参与 runnable 统计。
     * 这样 ready_threads 更接近真实 worker 数。
     */
    for (int i = 0; i < threads; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        if (ret != tids[i] || code != 0) {
            puts("[alpha_arg] FAIL: thread_join\n");
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
        role,
        threads,
        DEFAULT_TICKETS,
        total
    );

    return 0;
}

static void sample_children(
    int alpha,
    int proc_count,
    int pids[],
    int threads[]
) {
    usize sample_tick = global_end_tick - 20;

    while (get_ticks() < sample_tick) {
        sleep(1);
    }

    struct sched_proc_stat st;

    for (int i = 0; i < proc_count; i++) {
        if (get_process_sched_stat(pids[i], &st) == 0) {
            print_sample_line(
                alpha,
                i,
                threads[i],
                DEFAULT_TICKETS,
                &st
            );
        } else {
            puts("[alpha_sample] FAIL: get_process_sched_stat role=");
            put_int(i);
            puts("\n");
        }
    }
}

static int run_one_case(int alpha, int proc_count, int threads[]) {
    puts("\n[alpha_arg] run alpha=");
    put_int(alpha);
    puts(" procs=");
    put_int(proc_count);
    puts(" threads=");

    for (int i = 0; i < proc_count; i++) {
        put_int(threads[i]);
        if (i + 1 < proc_count) {
            puts(",");
        }
    }

    puts("\n");

    if (set_sched_alpha(alpha) < 0) {
        puts("[alpha_arg] FAIL: set_sched_alpha\n");
        return 1;
    }

    if (reset_sched_stat() < 0) {
        puts("[alpha_arg] FAIL: reset_sched_stat\n");
        return 1;
    }

    usize now = get_ticks();
    global_start_tick = now + START_DELAY_TICKS;
    global_end_tick = global_start_tick + TEST_TICKS;

    int pids[MAX_PROCS];

    for (int i = 0; i < proc_count; i++) {
        int pid = fork();

        if (pid < 0) {
            puts("[alpha_arg] FAIL: fork\n");
            return 1;
        }

        if (pid == 0) {
            int code = run_workload_process(alpha, i, threads[i]);
            exit(code);
        }

        pids[i] = pid;
    }

    /*
     * 父进程在 workload 还在运行时采样。
     * 这样 ready/effective 能看到每个进程的 runnable threads。
     */
    sample_children(alpha, proc_count, pids, threads);

    for (int i = 0; i < proc_count; i++) {
        int code = -1;
        int ret = waitpid(pids[i], &code, 0);

        if (ret != pids[i] || code != 0) {
            puts("[alpha_arg] FAIL: child failed role=");
            put_int(i);
            puts(" ret=");
            put_int(ret);
            puts(" code=");
            put_int(code);
            puts("\n");
            return 1;
        }
    }

    return 0;
}

int main(int argc, char **argv) {
    puts("alpha_arg_test start\n");
    puts("usage: alpha_arg_test <alpha> <threads0> <threads1> ...\n");
    puts("example: alpha_arg_test 50 1 3 5\n");

    if (argc < 4) {
        puts("[alpha_arg] FAIL: need alpha and at least two thread counts\n");
        return 1;
    }

    int alpha = 0;

    if ((alpha = parse_int(argv[1])) < 0) {
        puts("[alpha_arg] FAIL: bad alpha\n");
        return 1;
    }

    // if (!(alpha == 0 || alpha == 25 || alpha == 50 || alpha == 75 || alpha == 100)) {
    //     puts("[alpha_arg] FAIL: alpha must be 0,25,50,75,100\n");
    //     return 1;
    // }

    int proc_count = argc - 2;

    if (proc_count > MAX_PROCS) {
        puts("[alpha_arg] FAIL: too many processes\n");
        return 1;
    }

    int threads[MAX_PROCS];

    for (int i = 0; i < proc_count; i++) {
        if ((threads[i] = parse_int(argv[i + 2])) < 0) {
            puts("[alpha_arg] FAIL: bad thread count\n");
            return 1;
        }

        if (threads[i] <= 0 || threads[i] > MAX_THREADS) {
            puts("[alpha_arg] FAIL: thread count out of range\n");
            return 1;
        }
    }

    int ret = run_one_case(alpha, proc_count, threads);

    set_sched_alpha(50);

    if (ret != 0) {
        return 1;
    }

    puts("\nalpha_arg_test PASS\n");
    return 0;
}