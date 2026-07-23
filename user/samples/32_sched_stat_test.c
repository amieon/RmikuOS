#include "user.h"

int main(void) {
    puts("sched_stat_test start\n");

    set_my_tickets(233);

    struct sched_proc_stat st;

    if (get_process_sched_stat(getpid(), &st) < 0) {
        puts("FAIL: get_process_sched_stat\n");
        return 1;
    }

    puts("pid=");
    put_int(st.pid);
    puts(" tickets=");
    put_int(st.tickets);
    puts(" effective=");
    put_int(st.effective_tickets);
    puts(" ready=");
    put_int(st.ready_threads);
    puts(" alpha=");
    put_int(st.alpha);
    puts(" run_ticks=");
    put_int(st.run_ticks);
    puts(" pass=");
    put_int(st.pass);
    puts(" stride=");
    put_int(st.stride);
    puts("\n");

    if (st.pid != getpid()) {
        puts("FAIL: bad pid\n");
        return 1;
    }

    if (st.tickets != 233) {
        puts("FAIL: bad tickets\n");
        return 1;
    }

    puts("sched_stat_test PASS\n");
    return 0;
}