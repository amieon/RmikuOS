#pragma once

#include "sys.h"
#include "lock.h"

#define THREAD_STACK_SIZE (64 * 1024)
#define THREAD_MAX_RECORDS 128

struct thread_stack_record {
    int used;
    int tid;
    void *stack_base;
    usize stack_size;
};

static struct thread_stack_record thread_stack_records[THREAD_MAX_RECORDS];
static mutex_t thread_stack_lock = MUTEX_INIT;



//高级 thread_create/thread_join 会在外面自动管理 mmap 栈。
static inline isize sys_thread_create_raw(
    usize entry,
    usize arg0,
    usize arg1,
    usize stack_top
) {
    return syscall6(
        SYS_THREAD_CREATE,
        entry,
        arg0,
        arg1,
        stack_top,
        0,
        0
    );
}

static inline void sys_thread_exit_raw(int code) {
    syscall3(
        SYS_THREAD_EXIT,
        (usize)code,
        0,
        0
    );

    for (;;) {}
}

static inline isize sys_thread_join_raw(int tid, int *exit_code) {
    return syscall3(
        SYS_THREAD_JOIN,
        (usize)tid,
        (usize)exit_code,
        0
    );
}

/*
 * 用户态线程入口 trampoline。
 *
 * 内核实际跳到 __thread_entry(func, arg)，
 * 然后由这里调用真正的线程函数。
 */
static inline void __thread_entry(void (*func)(void *), void *arg) {
    func(arg);
    sys_thread_exit_raw(0);

    for (;;) {}
}

static inline int thread_stack_record_add(
    int tid,
    void *stack_base,
    usize stack_size
) {
    mutex_lock(&thread_stack_lock);

    for (int i = 0; i < THREAD_MAX_RECORDS; i++) {
        if (!thread_stack_records[i].used) {
            thread_stack_records[i].used = 1;
            thread_stack_records[i].tid = tid;
            thread_stack_records[i].stack_base = stack_base;
            thread_stack_records[i].stack_size = stack_size;

            mutex_unlock(&thread_stack_lock);
            return 0;
        }
    }

    mutex_unlock(&thread_stack_lock);
    return -1;
}

static inline int thread_stack_record_take(
    int tid,
    void **stack_base,
    usize *stack_size
) {
    mutex_lock(&thread_stack_lock);

    for (int i = 0; i < THREAD_MAX_RECORDS; i++) {
        if (thread_stack_records[i].used &&
            thread_stack_records[i].tid == tid) {
            *stack_base = thread_stack_records[i].stack_base;
            *stack_size = thread_stack_records[i].stack_size;

            thread_stack_records[i].used = 0;
            thread_stack_records[i].tid = -1;
            thread_stack_records[i].stack_base = 0;
            thread_stack_records[i].stack_size = 0;

            mutex_unlock(&thread_stack_lock);
            return 0;
        }
    }

    mutex_unlock(&thread_stack_lock);
    return -1;
}

static inline int thread_create(void (*func)(void *), void *arg) {
    void *stack_base = mmap(
        THREAD_STACK_SIZE,
        PROT_READ | PROT_WRITE
    );

    if ((isize)stack_base < 0) {
        return -1;
    }

    usize stack_top = (usize)stack_base + THREAD_STACK_SIZE;

    int tid = (int)sys_thread_create_raw(
        (usize)__thread_entry,
        (usize)func,
        (usize)arg,
        stack_top
    );

    if (tid < 0) {
        munmap(stack_base, THREAD_STACK_SIZE);
        return -1;
    }

    if (thread_stack_record_add(
            tid,
            stack_base,
            THREAD_STACK_SIZE
        ) < 0) {
        /*
         * 线程已经创建成功，不能 munmap 它正在用的栈。
         * 第一版选择保留线程运行，只打印 warning，一个小TODO
         */
        puts("[thread] warning: stack record table full\n");
    }

    return tid;
}

static inline void thread_exit(int code) {
    sys_thread_exit_raw(code);

    for (;;) {}
}

static inline int thread_join(int tid, int *exit_code) {
    int ret = (int)sys_thread_join_raw(tid, exit_code);

    if (ret < 0) {
        return ret;
    }

    void *stack_base = 0;
    usize stack_size = 0;

    if (thread_stack_record_take(
            tid,
            &stack_base,
            &stack_size
        ) == 0) {
        if (stack_base && stack_size) {
            if (munmap(stack_base, stack_size) < 0) {
                puts("[thread] warning: stack munmap failed\n");
            }
        }
    }

    return ret;
}


static inline int set_thread_tickets(int tid, int tickets) {
    return syscall3(SYS_SET_THREAD_TICKETS, tid, tickets, 0);
}

static inline int get_thread_tickets(int tid) {
    return syscall3(SYS_GET_THREAD_TICKETS, tid, 0, 0);
}