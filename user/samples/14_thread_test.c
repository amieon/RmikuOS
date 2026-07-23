#include "user.h"

#define STACK_SIZE 16384


// static unsigned char stack1[STACK_SIZE] __attribute__((aligned(16))) = {1};
// static unsigned char stack2[STACK_SIZE] __attribute__((aligned(16))) = {2};

static void burn_cpu(int rounds) {
    volatile unsigned long x = 1;

    for (int i = 0; i < rounds; i++) {
        x = x * 1103515245 + 12345;
        x ^= x >> 7;
        x ^= x << 9;
    }

    if (x == 0xdeadbeef) {
        write(1, "impossible\n", 11);
    }
}


static void print_line(const char *name, int round) {
    char buf[128];
    int pos = 0;

    pos = append_str(buf, pos, "[");
    pos = append_str(buf, pos, name);
    pos = append_str(buf, pos, "] pid=");
    pos = append_int(buf, pos, getpid());
    pos = append_str(buf, pos, " round=");
    pos = append_int(buf, pos, round);
    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static void worker(void *arg) {
    const char *name = (const char *)arg;

    for (int i = 0; i < 20; i++) {
        print_line(name, i);
        burn_cpu(100000);
    }

    thread_exit(100);
}

int main(int argc, char *argv[]) {
    // puts("stack1_top=");
    // put_hex((usize)(stack1 + STACK_SIZE));
    // puts("\n");


    puts("thread_test start\n");

    int t1 = thread_create(worker, "thread1"/*, stack1 + STACK_SIZE*/);
    int t2 = thread_create(worker, "thread2"/*, stack2 + STACK_SIZE*/);

    puts("created threads: ");
    put_int(t1);
    puts(", ");
    put_int(t2);
    puts("\n");

    for (int i = 0; i < 20; i++) {
        print_line("main   ", i);
        burn_cpu(100000);
    }

    int code1 = -1;
    int code2 = -1;

    int j1 = thread_join(t1, &code1);
    int j2 = thread_join(t2, &code2);

    puts("join t1=");
    put_int(j1);
    puts(" code=");
    put_int(code1);
    puts("\n");

    puts("join t2=");
    put_int(j2);
    puts(" code=");
    put_int(code2);
    puts("\n");

    puts("thread_test PASS\n");
    return 0;
}