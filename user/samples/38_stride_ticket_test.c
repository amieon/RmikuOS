#include "user.h"

#define MAX_PROCS 8

#define START_DELAY_TICKS 80
#define TEST_TICKS 600
#define SAMPLE_BEFORE_END_TICKS 20

#define CPU_BURN_ITERS 12000

static volatile usize global_start_tick;
static volatile usize global_end_tick;

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

static void print_run_header(int proc_count, int *tickets) {
    char buf[512];
    int pos = 0;

    pos = append_str(buf, pos, "\n[stride_ticket] run procs=");
    pos = append_usize(buf, pos, (usize)proc_count);

    pos = append_str(buf, pos, " tickets=");

    for (int i = 0; i < proc_count; i++) {
        if (i > 0) {
            pos = append_str(buf, pos, ",");
        }

        pos = append_usize(buf, pos, (usize)tickets[i]);
    }

    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static void print_sample_line(
    int role,
    int pid,
    int tickets,
    int expected_per_1000,
    struct sched_proc_stat *st
) {
    char buf[512];
    int pos = 0;

    pos = append_str(buf, pos, "[stride_sample] role=");
    pos = append_usize(buf, pos, (usize)role);

    pos = append_str(buf, pos, " pid=");
    pos = append_usize(buf, pos, (usize)pid);

    pos = append_str(buf, pos, " tickets=");
    pos = append_usize(buf, pos, (usize)tickets);

    pos = append_str(buf, pos, " expected_per_1000=");
    pos = append_usize(buf, pos, (usize)expected_per_1000);

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
    int role,
    int pid,
    int tickets,
    usize work
) {
    char buf[384];
    int pos = 0;

    pos = append_str(buf, pos, "[stride_result] role=");
    pos = append_usize(buf, pos, (usize)role);

    pos = append_str(buf, pos, " pid=");
    pos = append_usize(buf, pos, (usize)pid);

    pos = append_str(buf, pos, " tickets=");
    pos = append_usize(buf, pos, (usize)tickets);

    pos = append_str(buf, pos, " work=");
    pos = append_usize(buf, pos, work);

    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static int run_worker(int role, int tickets) {
    if (set_my_tickets(tickets) < 0) {
        puts("[stride_ticket] FAIL: set_my_tickets\n");
        return 1;
    }

    wait_until(global_start_tick);

    usize work = 0;

    while (get_ticks() < global_end_tick) {
        burn_iters(CPU_BURN_ITERS);
        work++;

        /*
         * 稀疏 yield，避免完全依赖时钟抢占。
         * 所有 worker 规则相同，因此对 tickets 比例影响很小。
         */
        if ((work & 0x7f) == 0) {
            yield();
        }
    }

    print_result_line(role, getpid(), tickets, work);

    return 0;
}

static int run_one_case(int proc_count, int *tickets) {
    int pids[MAX_PROCS];
    int total_tickets = 0;

    for (int i = 0; i < proc_count; i++) {
        total_tickets += tickets[i];
    }

    print_run_header(proc_count, tickets);

    /*
     * 这里强制 alpha=0，把实验聚焦在基础 stride tickets。
     * 本实验每个进程都是单线程，alpha 本来也不应该影响结果。
     */
    if (set_sched_alpha(0) < 0) {
        puts("[stride_ticket] FAIL: set_sched_alpha\n");
        return 1;
    }

    if (reset_sched_stat() < 0) {
        puts("[stride_ticket] FAIL: reset_sched_stat\n");
        return 1;
    }

    usize now = get_ticks();
    global_start_tick = now + START_DELAY_TICKS;
    global_end_tick = global_start_tick + TEST_TICKS;

    for (int i = 0; i < proc_count; i++) {
        int pid = fork();

        if (pid < 0) {
            puts("[stride_ticket] FAIL: fork\n");
            return 1;
        }

        if (pid == 0) {
            int code = run_worker(i, tickets[i]);
            exit(code);
        }

        pids[i] = pid;
    }

    usize sample_tick = global_end_tick - SAMPLE_BEFORE_END_TICKS;

    while (get_ticks() < sample_tick) {
        sleep(1);
    }

    for (int i = 0; i < proc_count; i++) {
        struct sched_proc_stat st;

        if (get_process_sched_stat(pids[i], &st) == 0) {
            int expected_per_1000 = tickets[i] * 1000 / total_tickets;

            print_sample_line(
                i,
                pids[i],
                tickets[i],
                expected_per_1000,
                &st
            );
        } else {
            puts("[stride_ticket] FAIL: get_process_sched_stat\n");
        }
    }

    for (int i = 0; i < proc_count; i++) {
        int code = -1;
        int ret = waitpid(pids[i], &code, 0);

        if (ret != pids[i] || code != 0) {
            puts("[stride_ticket] FAIL: child failed\n");
            puts("i=");
            put_int(i);
            puts(" pid=");
            put_int(pids[i]);
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
    puts("stride_ticket_test start\n");
    puts("usage: stride_ticket_test <tickets0> <tickets1> ...\n");
    puts("example: stride_ticket_test 100 200 300\n");

    if (argc < 3) {
        puts("[stride_ticket] FAIL: need at least 2 ticket args\n");
        return 1;
    }

    int proc_count = argc - 1;

    if (proc_count > MAX_PROCS) {
        puts("[stride_ticket] FAIL: too many procs\n");
        return 1;
    }

    int tickets[MAX_PROCS];

    for (int i = 0; i < proc_count; i++) {
        if (parse_int_ptr(argv[i + 1], &tickets[i]) < 0) {
            puts("[stride_ticket] FAIL: bad ticket arg\n");
            return 1;
        }

        if (tickets[i] <= 0) {
            puts("[stride_ticket] FAIL: ticket must be positive\n");
            return 1;
        }
    }

    int ret = run_one_case(proc_count, tickets);

    set_sched_alpha(50);

    if (ret != 0) {
        return 1;
    }

    puts("\nstride_ticket_test PASS\n");

    return 0;
}