#include "user.h"

int main(void) {
    puts("reset_sched_stat_test start\n");

    struct sched_proc_stat st;

    for (int i = 0; i < 50; i++) {
        yield();
    }

    if (get_process_sched_stat(getpid(), &st) < 0) {
        puts("FAIL: get stat before reset\n");
        return 1;
    }

    puts("before reset run_ticks=");
    put_int(st.run_ticks);
    puts("\n");

    if (reset_sched_stat() < 0) {
        puts("FAIL: reset_sched_stat\n");
        return 1;
    }

    if (get_process_sched_stat(getpid(), &st) < 0) {
        puts("FAIL: get stat after reset\n");
        return 1;
    }

    puts("after reset run_ticks=");
    put_int(st.run_ticks);
    puts("\n");

    if (st.run_ticks != 0) {
        puts("FAIL: run_ticks not reset\n");
        return 1;
    }

    puts("reset_sched_stat_test PASS\n");
    return 0;
}