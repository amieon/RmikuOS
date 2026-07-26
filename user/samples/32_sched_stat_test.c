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
    printf("%d", st.pid);
    puts(" tickets=");
    printf("%d", st.tickets);
    puts(" effective=");
    printf("%d", st.effective_tickets);
    puts(" ready=");
    printf("%d", st.ready_threads);
    puts(" alpha=");
    printf("%d", st.alpha);
    puts(" run_ticks=");
    printf("%d", st.run_ticks);
    puts(" pass=");
    printf("%d", st.pass);
    puts(" stride=");
    printf("%d", st.stride);
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