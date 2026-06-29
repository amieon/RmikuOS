#pragma once
#ifdef __cplusplus
extern "C" {
#endif



#include "syscall.h"

/* ---- tickets ---- */

static inline int set_process_tickets(int pid, int tickets) {
    return syscall3(SYS_SET_PROCESS_TICKETS, (usize)pid, tickets, 0);
}

static inline int set_my_tickets(int tickets) {
    return syscall3(SYS_SET_MY_TICKETS, tickets, 0, 0);
}

static inline int get_process_tickets(int pid) {
    return syscall3(SYS_GET_PROCESS_TICKETS, (usize)pid, 0, 0);
}

static inline int get_my_tickets(void) {
    return syscall3(SYS_GET_MY_TICKETS, 0, 0, 0);
}

static inline int set_thread_tickets(int tid, int tickets) {
    return syscall3(SYS_SET_THREAD_TICKETS, (usize)tid, tickets, 0);
}

static inline int get_thread_tickets(int tid) {
    return syscall3(SYS_GET_THREAD_TICKETS, (usize)tid, 0, 0);
}

/* ---- alpha 旋钮 ---- */

static inline int set_sched_alpha(int alpha) {
    return syscall3(SYS_SET_SCHED_ALPHA, alpha, 0, 0);
}

static inline int get_sched_alpha(void) {
    return syscall3(SYS_GET_SCHED_ALPHA, 0, 0, 0);
}

/* ---- 调度统计 ---- */

struct sched_proc_stat {
    int pid;
    int tickets;
    int effective_tickets;
    int ready_threads;
    int alpha;

    unsigned long run_ticks;
    unsigned long pass;
    unsigned long stride;
};

static inline int get_process_sched_stat(int pid, struct sched_proc_stat *stat) {
    return syscall3(
        SYS_GET_PROCESS_SCHED_STAT,
        (usize)pid,
        (usize)stat,
        0
    );
}

static inline int reset_sched_stat(void) {
    return syscall3(SYS_RESET_SCHED_STAT, 0, 0, 0);
}

/* ---- 时钟节拍 ---- */

static inline usize get_ticks(void) {
    return syscall3(SYS_GET_TICKS, 0, 0, 0);
}
#ifdef __cplusplus
}
#endif
