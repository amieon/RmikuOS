#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "mem.h"
#include "lock.h"
#include "io.h"
#include "stdio.h"

#define THREAD_STACK_SIZE (64 * 1024)
#define THREAD_MAX_RECORDS 512

struct thread_stack_record {
    int used;
    int tid;
    void *stack_base;
    usize stack_size;
};

/* 表和锁必须跨 TU 唯一, 定义在 lib/thread.c。
 * 头里写 static 的后果: 每个 TU 各得 12 KiB 数组 + 一把各管各的锁, 跨 TU 的
 * create/join 查不到记录 -> thread_join 静默跳过 munmap, 每次泄漏 64 KiB 栈。 */
extern struct thread_stack_record thread_stack_records[THREAD_MAX_RECORDS];
extern mutex_t thread_stack_lock;

/* 高级 thread_create/thread_join 会在外面自动管理 mmap 栈。 */
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
 * 内核实际跳到 __thread_entry(func, arg),
 * 然后由这里调用真正的线程函数。
 */
static inline void __thread_entry(void (*func)(void *), void *arg) {
    func(arg);
    sys_thread_exit_raw(0);

    for (;;) {}
}

/* 实现在 lib/thread.c —— 和表放在同一个 TU, 不必每个 TU 重复 emit。 */
int thread_stack_record_add(
    int tid,
    void *stack_base,
    usize stack_size
);

int thread_stack_record_take(
    int tid,
    void **stack_base,
    usize *stack_size
);

static inline int gettid(void) {
    return (int)syscall3(SYS_GETTID, 0, 0, 0);
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
         * 线程已经创建成功,不能 munmap 它正在用的栈。
         * 第一版选择保留线程运行,只打印 warning,一个小 TODO
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
#ifdef __cplusplus
}
#endif
