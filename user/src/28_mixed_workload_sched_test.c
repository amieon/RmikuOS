#include "user.h"

#define CONTROL_THREADS 1
#define AI_THREADS 9
#define LOGGER_THREADS 4

#define MAX_THREADS 9

#define CONTROL_TICKETS 300
#define AI_TICKETS 100
#define LOGGER_TICKETS 50

#define TEST_TICKS 400
#define BURN_ITERS 12000

static volatile int start_flag;
static volatile int stop_flag;
static volatile usize counters[MAX_THREADS];

struct arg {
    int id;
};

static struct arg args[MAX_THREADS];

static int append_str(char *buf, int pos, const char *s) {
    while (*s) {
        buf[pos++] = *s++;
    }
    return pos;
}

static int append_usize(char *buf, int pos, usize x) {
    char tmp[32];
    int n = 0;

    if (x == 0) {
        buf[pos++] = '0';
        return pos;
    }

    while (x > 0) {
        tmp[n++] = '0' + (x % 10);
        x /= 10;
    }

    while (n > 0) {
        buf[pos++] = tmp[--n];
    }

    return pos;
}

static void print_result(const char *name, int threads, int tickets, usize total) {
    char buf[192];
    int pos = 0;

    pos = append_str(buf, pos, "[mixed_sched] ");
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

static int run_workload(const char *name, int threads, int tickets) {
    if (threads <= 0 || threads > MAX_THREADS) {
        puts("[mixed_sched] FAIL: bad thread count\n");
        return 1;
    }

    if (set_process_tickets(tickets) < 0) {
        puts("[mixed_sched] FAIL: set_process_tickets\n");
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
            puts("[mixed_sched] FAIL: thread_create\n");
            return 2;
        }
    }

    /*
     * 子进程主线程进入 sleep，worker 线程保持 Ready/Running。
     * 这样 process ready_thread_count 主要就是 worker 数量。
     */
    start_flag = 1;
    sleep(TEST_TICKS);
    stop_flag = 1;

    usize total = 0;

    for (int i = 0; i < threads; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        if (ret != tids[i] || code != 0) {
            puts("[mixed_sched] FAIL: thread_join i=");
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

    print_result(name, threads, tickets, total);

    return 0;
}

int main(void) {
    puts("mixed_workload_sched_test start\n");
    puts("model: effective_tickets = base_tickets * sqrt(ready_threads)\n");
    puts("control: 1 thread, tickets=300 => 300 * sqrt(1) = 300\n");
    puts("ai:      9 threads, tickets=100 => 100 * sqrt(9) = 300\n");
    puts("logger:  4 threads, tickets=50  =>  50 * sqrt(4) = 100\n");
    puts("expected total_work ratio: control : ai : logger ~= 3 : 3 : 1\n");

    int pid_control = fork();

    if (pid_control < 0) {
        puts("FAIL: fork control\n");
        return 1;
    }

    if (pid_control == 0) {
        int code = run_workload(
            "control",
            CONTROL_THREADS,
            CONTROL_TICKETS
        );
        exit(code);
    }

    int pid_ai = fork();

    if (pid_ai < 0) {
        puts("FAIL: fork ai\n");
        return 1;
    }

    if (pid_ai == 0) {
        int code = run_workload(
            "ai",
            AI_THREADS,
            AI_TICKETS
        );
        exit(code);
    }

    int pid_logger = fork();

    if (pid_logger < 0) {
        puts("FAIL: fork logger\n");
        return 1;
    }

    if (pid_logger == 0) {
        int code = run_workload(
            "logger",
            LOGGER_THREADS,
            LOGGER_TICKETS
        );
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

    puts("mixed_workload_sched_test done\n");
    puts("manual check: control and ai should be close; logger should be about one third of them\n");

    return 0;
}