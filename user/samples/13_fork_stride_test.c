#include "user.h"

static void print_round_line(const char *name, int pid, int round) {
    char buf[128];
    int pos = 0;

    pos = append_str(buf, pos, "[");
    pos = append_str(buf, pos, name);
    pos = append_str(buf, pos, "] pid=");
    pos = append_int(buf, pos, pid);
    pos = append_str(buf, pos, " round=");
    pos = append_int(buf, pos, round);
    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}


static void burn_cpu(int rounds) {
    volatile unsigned long x = 1;

    for (int i = 0; i < rounds; i++) {
        x = x * 1103515245 + 12345;
        x ^= x >> 7;
        x ^= x << 9;
    }

    // 防止编译器太聪明直接优化掉
    if (x == 0xdeadbeef) {
        puts("impossible\n");
    }
}

static void worker(const char *name, int rounds, int burn) {
    int pid = getpid();

    for (int i = 0; i < rounds; i++) {
        print_round_line(name, pid, i);
        burn_cpu(burn);
    }
}

int main(int argc, char *argv[]) {
    int rounds = 30;
    int burn = 120000;

    if (argc >= 2) {
        rounds = parse_int(argv[1]);
    }

    if (argc >= 3) {
        burn = parse_int(argv[2]);
    }

    puts("This test creates two CPU-bound processes.\n");
    puts("If process-level stride scheduling works, parent and child should interleave.\n");
    puts("rounds=");
    put_int(rounds);
    puts(", burn=");
    put_int(burn);
    puts("\n\n");

    int child = fork();

    if (child < 0) {
        puts("[test] fork failed\n");
        return 1;
    }

    if (child == 0) {
        worker("child ", rounds, burn);
        puts("[child ] exit\n");
        exit(0);
        return 0;
    }

    worker("parent", rounds, burn);

    int exit_code = -1;
    int waited = waitpid(child, &exit_code, 0);

    puts("[parent] waitpid returned pid=");
    put_int(waited);
    puts(", exit_code=");
    put_int(exit_code);
    puts("\n");

    if (waited != child || exit_code != 0) {
        puts("[test] FAIL: waitpid result wrong\n");
        return 1;
    }

    puts("TEST PASSn");
    return 0;
}