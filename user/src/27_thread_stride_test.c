#include "user.h"

#define THREADS 4
#define TEST_TICKS 300
#define BURN_ITERS 12000


#define ENABLE_THREAD_TICKETS 1

static volatile int start_flag;
static volatile int stop_flag;
static volatile usize counters[THREADS];

struct arg {
    int id;
};

static struct arg args[THREADS];

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

static void print_counter(int tid, usize value) {
    char buf[128];
    int pos = 0;

    pos = append_str(buf, pos, "[thread_stride] tid_index=");
    pos = append_usize(buf, pos, (usize)tid);
    pos = append_str(buf, pos, " work=");
    pos = append_usize(buf, pos, value);
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

int main(void) {
    puts("thread_stride_test start\n");

#if ENABLE_THREAD_TICKETS
    puts("mode: weighted thread stride, expected ratio ~= 1:2:4:8\n");
#else
    puts("mode: equal thread stride, expected ratio ~= 1:1:1:1\n");
#endif

    start_flag = 0;
    stop_flag = 0;

    for (int i = 0; i < THREADS; i++) {
        counters[i] = 0;
    }

    int tids[THREADS];

    for (int i = 0; i < THREADS; i++) {
        args[i].id = i;

        tids[i] = thread_create(worker, &args[i]);

        if (tids[i] < 0) {
            puts("FAIL: thread_create\n");
            return 1;
        }
    }

#if ENABLE_THREAD_TICKETS
    int tickets[THREADS] = {100, 200, 400, 800};

    for (int i = 0; i < THREADS; i++) {
        if (set_thread_tickets(tids[i], tickets[i]) < 0) {
            puts("FAIL: set_thread_tickets\n");
            return 1;
        }
    }
#endif

    start_flag = 1;

    sleep(TEST_TICKS);

    stop_flag = 1;

    for (int i = 0; i < THREADS; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        if (ret != tids[i] || code != 0) {
            puts("FAIL: thread_join i=");
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

    usize total = 0;

    for (int i = 0; i < THREADS; i++) {
        print_counter(i, counters[i]);
        total += counters[i];
    }

    puts("[thread_stride] total=");
    put_int(total);
    puts("\n");

#if ENABLE_THREAD_TICKETS
    puts("manual check: work ratio should be roughly 1:2:4:8\n");
#else
    puts("manual check: all thread work values should be close\n");
#endif

    puts("thread_stride_test PASS\n");
    return 0;
}