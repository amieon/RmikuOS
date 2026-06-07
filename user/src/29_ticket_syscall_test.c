#include "user.h"

static volatile int stop_worker;

static void worker(void *arg) {
    while (!stop_worker) {
        yield();
    }

    thread_exit(0);
}


int main(void) {
    puts("tickets_syscall_test start\n");

    int pid = getpid();

    int old_my = get_my_tickets();
    int old_proc = get_process_tickets(pid);

    puts("initial my_tickets=");
    put_int(old_my);
    puts(" process_tickets=");
    put_int(old_proc);
    puts("\n");

    if (old_my != old_proc) {
        puts("FAIL: get_my_tickets != get_process_tickets(getpid())\n");
        return 1;
    }

    if (set_my_tickets(123) < 0) {
        puts("FAIL: set_my_tickets\n");
        return 1;
    }

    if (get_my_tickets() != 123) {
        puts("FAIL: get_my_tickets after set_my_tickets\n");
        return 1;
    }

    if (get_process_tickets(pid) != 123) {
        puts("FAIL: get_process_tickets after set_my_tickets\n");
        return 1;
    }

    if (set_process_tickets(pid, 234) < 0) {
        puts("FAIL: set_process_tickets current pid\n");
        return 1;
    }

    if (get_my_tickets() != 234) {
        puts("FAIL: get_my_tickets after set_process_tickets\n");
        return 1;
    }

    stop_worker = 0;

    int tid = thread_create(worker, 0);

    if (tid < 0) {
        puts("FAIL: thread_create\n");
        return 1;
    }

    int old_thread = get_thread_tickets(tid);

    puts("initial thread_tickets=");
    put_int(old_thread);
    puts("\n");

    if (old_thread <= 0) {
        puts("FAIL: bad initial thread tickets\n");
        return 1;
    }

    if (set_thread_tickets(tid, 345) < 0) {
        puts("FAIL: set_thread_tickets\n");
        return 1;
    }

    if (get_thread_tickets(tid) != 345) {
        puts("FAIL: get_thread_tickets after set_thread_tickets\n");
        return 1;
    }

    stop_worker = 1;

    int code = -1;
    int ret = thread_join(tid, &code);

    if (ret != tid || code != 0) {
        puts("FAIL: thread_join\n");
        puts("ret=");
        put_int(ret);
        puts(" code=");
        put_int(code);
        puts("\n");
        return 1;
    }

    puts("tickets_syscall_test PASS\n");
    return 0;
}