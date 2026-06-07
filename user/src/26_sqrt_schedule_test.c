#include "user.h"




#define MANY_THREADS 25
#define ONE_THREADS 1
#define MAX_THREADS MANY_THREADS

#define TEST_TICKS 300
#define BURN_ITERS 12000

static volatile int stop_flag;
static volatile usize counters[MAX_THREADS];

struct arg {
    int id;
};

static struct arg args[MAX_THREADS];


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


static void print_result(const char *name, int threads, usize total) {
    char buf[160];
    int pos = 0;

    pos = append_str(buf, pos, "[sqrt_sched] ");
    pos = append_str(buf, pos, name);
    pos = append_str(buf, pos, " pid=");
    pos = append_usize(buf, pos, (usize)getpid());
    pos = append_str(buf, pos, " threads=");
    pos = append_usize(buf, pos, (usize)threads);
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

    while (!stop_flag) {
        burn();
        counters[id]++;

        //偶尔 yield，让调度交错更明显。
        //不要太频繁，否则测试变成 yield syscall 压测。
        if ((counters[id] & 0x3f) == 0) {
            yield();
        }
    }

    thread_exit(0);
}

static int run_child(const char *name, int nthreads) {
    int tids[MAX_THREADS];

    stop_flag = 0;

    for (int i = 0; i < MAX_THREADS; i++) {
        counters[i] = 0;
    }

    for (int i = 0; i < nthreads; i++) {
        args[i].id = i;

        tids[i] = thread_create(worker, &args[i]);

        if (tids[i] < 0) {
            puts("[sqrt_sched] FAIL: thread_create\n");
            return 1;
        }
    }

    sleep(TEST_TICKS);

    stop_flag = 1;

    usize total = 0;

    for (int i = 0; i < nthreads; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        if (ret != tids[i] || code != 0) {
            puts("[sqrt_sched] FAIL: thread_join i=");
            put_int(i);
            puts(" tid=");
            put_int(tids[i]);
            puts(" ret=");
            put_int(ret);
            puts(" code=");
            put_int(code);
            puts("\n");
            return 2;
        }

        total += counters[i];
    }

    print_result(name, nthreads, total);

    return 0;
}

int main(void) {
    puts("sqrt_sched_test start\n");
    puts("expect process work ratio: many_threads : one_thread ~= sqrt(25) : sqrt(1) = 25 : 1\n");
    puts("if ratio is close to 25:1, scheduler behaves like flat thread scheduling\n");
    puts("if ratio is close to 1:1, scheduler behaves like pure process scheduling\n");

    int pid_many = fork();

    if (pid_many < 0) {
        puts("FAIL: fork many\n");
        return 1;
    }

    if (pid_many == 0) {
        int code = run_child("many", MANY_THREADS);
        exit(code);
    }

    int pid_one = fork();

    if (pid_one < 0) {
        puts("FAIL: fork one\n");
        return 1;
    }

    if (pid_one == 0) {
        int code = run_child("one", ONE_THREADS);
        exit(code);
    }

    int code_many = -1;
    int code_one = -1;

    int ret_many = waitpid(pid_many, &code_many);
    int ret_one = waitpid(pid_one, &code_one);

    if (ret_many != pid_many || code_many != 0) {
        puts("FAIL: many child failed\n");
        puts("ret=");
        put_int(ret_many);
        puts(" code=");
        put_int(code_many);
        puts("\n");
        return 1;
    }

    if (ret_one != pid_one || code_one != 0) {
        puts("FAIL: one child failed\n");
        puts("ret=");
        put_int(ret_one);
        puts(" code=");
        put_int(code_one);
        puts("\n");
        return 1;
    }

    puts("sqrt_sched_test done\n");
    puts("manual check: total_work(many) / total_work(one) should be roughly around 5, not 25 or 1\n");

    return 0;
}