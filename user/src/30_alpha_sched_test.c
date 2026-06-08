#include "user.h"

#define CONTROL_THREADS 1
#define AI_THREADS 9
#define LOGGER_THREADS 4
#define MAX_THREADS 9

#define CONTROL_TICKETS 300
#define AI_TICKETS 100
#define LOGGER_TICKETS 50

#define TEST_TICKS 350
#define BURN_ITERS 12000

static volatile int start_flag;
static volatile int stop_flag;
static volatile usize counters[MAX_THREADS];

struct arg {
    int id;
};

static struct arg args[MAX_THREADS];


static void print_result(int alpha, const char *name, int threads, int tickets, usize total) {
    char buf[224];
    int pos = 0;

    pos = append_str(buf, pos, "[alpha_sched] alpha=");
    pos = append_usize(buf, pos, (usize)alpha);
    pos = append_str(buf, pos, " ");
    pos = append_str(buf, pos, name);
    pos = append_str(buf, pos, " pid=");
    pos = append_usize(buf, pos, (usize)getpid());
    pos = append_str(buf, pos, " threads=");
    pos = append_usize(buf, pos, (usize)threads);
    pos = append_str(buf, pos, " base_tickets=");
    pos = append_usize(buf, pos, (usize)tickets);
    pos = append_str(buf, pos, " total_work=");
    pos = append_usize(buf, pos, total);
    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static void burn(void) {
    volatile usize x = 0;

    for (int i = 0; i < BURN_ITERS; i++) {
        x = x * 1103515245u + 12345u + (usize)i;
    }

    (void)x;
}

static void worker(void *raw) {
    struct arg *a = (struct arg *)raw;
    int id = a->id;

    while (!start_flag) {
        yield();
    }

    while (!stop_flag) {
        burn();
        counters[id]++;

        if ((counters[id] & 0x3f) == 0) {
            yield();
        }
    }

    thread_exit(0);
}

static int run_workload(int alpha, const char *name, int threads, int tickets) {
    if (threads <= 0 || threads > MAX_THREADS) {
        puts("[alpha_sched] FAIL: bad thread count\n");
        return 1;
    }

    if (set_my_tickets(tickets) < 0) {
        puts("[alpha_sched] FAIL: set_my_tickets\n");
        return 1;
    }

    if (get_my_tickets() != tickets) {
        puts("[alpha_sched] FAIL: get_my_tickets mismatch\n");
        return 1;
    }

    start_flag = 0;
    stop_flag = 0;

    for (int i = 0; i < MAX_THREADS; i++) {
        counters[i] = 0;
    }

    int tids[MAX_THREADS];

    for (int i = 0; i < threads; i++) {
        args[i].id = i;

        tids[i] = thread_create(worker, &args[i]);

        if (tids[i] < 0) {
            puts("[alpha_sched] FAIL: thread_create\n");
            return 2;
        }
    }

    start_flag = 1;
    sleep(TEST_TICKS);
    stop_flag = 1;

    usize total = 0;

    for (int i = 0; i < threads; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        if (ret != tids[i] || code != 0) {
            puts("[alpha_sched] FAIL: thread_join\n");
            puts("i=");
            put_int(i);
            puts(" tid=");
            put_int(tids[i]);
            puts(" ret=");
            put_int(ret);
            puts(" code=");
            put_int(code);
            puts("\n");
            return 3;
        }

        total += counters[i];
    }

    print_result(alpha, name, threads, tickets, total);

    return 0;
}

static int run_one_alpha(int alpha) {
    puts("\n[alpha_sched] run alpha=");
    put_int(alpha);
    puts("\n");

    if (set_sched_alpha(alpha) < 0) {
        puts("FAIL: set_sched_alpha\n");
        return 1;
    }

    if (get_sched_alpha() != alpha) {
        puts("FAIL: get_sched_alpha mismatch\n");
        return 1;
    }

    if (alpha == 0) {
        puts("expected ratio: control:ai:logger ~= 6:2:1\n");
    } else if (alpha == 50) {
        puts("expected ratio: control:ai:logger ~= 3:3:1\n");
    } else if (alpha == 100) {
        puts("expected ratio: control:ai:logger ~= 3:9:2\n");
    }

    int pid_control = fork();

    if (pid_control < 0) {
        puts("FAIL: fork control\n");
        return 1;
    }

    if (pid_control == 0) {
        int code = run_workload(alpha, "control", CONTROL_THREADS, CONTROL_TICKETS);
        exit(code);
    }

    int pid_ai = fork();

    if (pid_ai < 0) {
        puts("FAIL: fork ai\n");
        return 1;
    }

    if (pid_ai == 0) {
        int code = run_workload(alpha, "ai", AI_THREADS, AI_TICKETS);
        exit(code);
    }

    int pid_logger = fork();

    if (pid_logger < 0) {
        puts("FAIL: fork logger\n");
        return 1;
    }

    if (pid_logger == 0) {
        int code = run_workload(alpha, "logger", LOGGER_THREADS, LOGGER_TICKETS);
        exit(code);
    }

    int code_control = -1;
    int code_ai = -1;
    int code_logger = -1;

    int ret_control = waitpid(pid_control, &code_control);
    int ret_ai = waitpid(pid_ai, &code_ai);
    int ret_logger = waitpid(pid_logger, &code_logger);

    if (ret_control != pid_control || code_control != 0) {
        puts("FAIL: control child failed\n");
        return 1;
    }

    if (ret_ai != pid_ai || code_ai != 0) {
        puts("FAIL: ai child failed\n");
        return 1;
    }

    if (ret_logger != pid_logger || code_logger != 0) {
        puts("FAIL: logger child failed\n");
        return 1;
    }

    return 0;
}

int main(void) {
    puts("alpha_sched_test start\n");
    puts("model: effective_tickets = base_tickets * ready_threads^alpha\n");
    puts("alpha values: 0, 50, 100 mean 0, 0.5, 1\n");

    if (run_one_alpha(0) < 0) {
        return 1;
    }

    if (run_one_alpha(50) < 0) {
        return 1;
    }

    if (run_one_alpha(100) < 0) {
        return 1;
    }

    /*
     * 恢复默认 sqrt，避免影响后续 shell 里其他测试。
     */
    set_sched_alpha(50);

    puts("\nalpha_sched_test PASS\n");
    puts("manual check:\n");
    puts("  alpha=0   should be close to 6:2:1\n");
    puts("  alpha=50  should be close to 3:3:1\n");
    puts("  alpha=100 should be close to 3:9:2\n");

    return 0;
}