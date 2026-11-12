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

/* 参数是全宽退出值：内核 17 号不做截断。thread_exit(int) 的整数退出码
 * 通过隐式提升进来，pthread_exit 的 void* 也走同一个槽。 */
static inline void sys_thread_exit_raw(usize retval) {
    syscall3(
        SYS_THREAD_EXIT,
        retval,
        0,
        0
    );

    for (;;) {}
}

/* 18 号唯一入口：内核回写 sizeof(usize) 字节的 TCB::retval。
 * retval 必须指向一个 8 字节对象，传 int* 会被踩栈。 */
static inline isize sys_thread_join_raw(int tid, void **retval) {
    return syscall3(
        SYS_THREAD_JOIN,
        (usize)tid,
        (usize)retval,
        0
    );
}

/* detach 的内核入口：后两个参数是本线程的用户栈匿名映射，内核在 reap 时
 * 替我们 remove_area。传 0/0 = 调用方自己管栈，只回收 TCB。
 * 返回裸 -errno，不过 rk_sys_ret；pthread_detach 会翻成正数错误码。 */
static inline isize sys_thread_detach_raw(
    int tid,
    usize stack_base,
    usize stack_size
) {
    return syscall3(
        SYS_THREAD_DETACH,
        (usize)tid,
        stack_base,
        stack_size
    );
}

/*
 * 用户态线程入口 trampoline。
 *
 * 内核实际跳到 __thread_entry(func, arg),
 * 然后由这里调用真正的线程函数。
 */
static inline void __thread_entry(void (*func)(void *), void *arg) {
    /* errno 本体放在本线程栈帧上，func() 返回前一直有效；绑定后任意
     * 深度的 errno 访问都落到这一份，不与其它线程互相覆盖。
     * 块内弱声明，不把 pthread.h 拉进 thread.h 的包含链。 */
    int tls_errno = 0;
    {
        extern void rk_tsd_bind_errno_slot(int *) __attribute__((weak));
        if (rk_tsd_bind_errno_slot) rk_tsd_bind_errno_slot(&tls_errno);
    }
    func(arg);
    /* POSIX: 线程退出前跑 TSD destructor 轮次。这里用块内弱声明，
     * 不把整个 pthread.h 拉进 thread.h 的包含链(thread.h 被 io.h 一类底层头用)。 */
    {
        extern void rk_tsd_run_destructors(void) __attribute__((weak));
        if (rk_tsd_run_destructors) rk_tsd_run_destructors();
    }
    sys_thread_exit_raw(0);

    for (;;) {}
}

/* pthread_create 的 trampoline：与 __thread_entry 唯一的区别是入口返回
 * void*，退出时把这个值全宽交给 SYS_THREAD_EXIT（内核存在 TCB::retval）。
 * pthread_exit() 从 func() 内部直接陷入、不回到这里，所以两条退出路径
 * 互斥，TSD destructor 不会跑两遍：pthread_exit 自己负责跑。 */
static inline void __pthread_entry(void *(*func)(void *), void *arg) {
    int tls_errno = 0;
    {
        extern void rk_tsd_bind_errno_slot(int *) __attribute__((weak));
        if (rk_tsd_bind_errno_slot) rk_tsd_bind_errno_slot(&tls_errno);
    }
    void *retval = func(arg);
    {
        extern void rk_tsd_run_destructors(void) __attribute__((weak));
        if (rk_tsd_run_destructors) rk_tsd_run_destructors();
    }
    sys_thread_exit_raw((usize)retval);

    for (;;) {}
}

/* 实现在 lib/thread.c —— 和表放在同一个 TU, 不必每个 TU 重复 emit。 */
int thread_stack_record_add(
    int tid,
    void *stack_base,
    usize stack_size
);

/* peek 不摘表：pthread_detach 要先拿到栈地址交给内核登记，等 syscall
 * 成功后再 take。若先 take 再发 syscall，失败时记录就丢了，join 也补不回来。 */
int thread_stack_record_peek(
    int tid,
    void **stack_base,
    usize *stack_size
);

int thread_stack_record_take(
    int tid,
    void **stack_base,
    usize *stack_size
);

static inline int gettid(void) {
    return (int)syscall3(SYS_GETTID, 0, 0, 0);
}

/* thread_create 与 pthread_create 共用：栈 mmap、栈记录表登记、失败回滚
 * 只有这一份。pthread_create 只是换个 trampoline(__pthread_entry) 和栈大小，
 * 若各自复制一遍，join/detach 查的却是同一张 thread_stack_records 表，
 * 漏登记的那条路径会静默泄漏 64 KiB 栈。 */
static inline int thread_spawn_with_entry(
    usize entry,
    usize func,
    usize arg,
    usize stack_size
) {
    void *stack_base = rk_anon_mmap(stack_size, PROT_READ | PROT_WRITE);

    if ((isize)stack_base < 0) {
        return -1;
    }

    usize stack_top = (usize)stack_base + stack_size;

    int tid = (int)sys_thread_create_raw(entry, func, arg, stack_top);

    if (tid < 0) {
        munmap(stack_base, stack_size);
        return -1;
    }

    if (thread_stack_record_add(tid, stack_base, stack_size) < 0) {
        /*
         * 线程已经创建成功,不能 munmap 它正在用的栈。
         * 第一版选择保留线程运行,只打印 warning,一个小 TODO
         */
        puts("[thread] warning: stack record table full\n");
    }

    return tid;
}

static inline int thread_create(void (*func)(void *), void *arg) {
    return thread_spawn_with_entry(
        (usize)__thread_entry,
        (usize)func,
        (usize)arg,
        THREAD_STACK_SIZE
    );
}

/* 直接退出也要跑 TSD destructor：detached 线程常用这条路径，漏掉的话
 * pthread_key_create 登记的清理函数永远不执行。__thread_entry 是另一条
 * 路径，它自己已经跑过一次；两条路互斥，不会重复。 */
static inline void thread_exit(int code) {
    {
        extern void rk_tsd_run_destructors(void) __attribute__((weak));
        if (rk_tsd_run_destructors) rk_tsd_run_destructors();
    }
    sys_thread_exit_raw((usize)code);

    for (;;) {}
}

/* 自有 API 的便利包装：内核只认全宽 retval，这里用 8 字节临时量接住再
 * 窄化给调用方的 int*。thread_exit(int) 的退出码本来就在 int 范围。
 * 对 detached 线程内核直接返 -EINVAL，这里早退、不会 munmap：栈已由内核
 * 在 reap 时解除映射，用户态再 munmap 一次就是重复释放同一段 VA。 */
static inline int thread_join(int tid, int *exit_code) {
    void *retval = 0;
    int ret = (int)sys_thread_join_raw(tid, &retval);

    if (ret < 0) {
        return ret;
    }

    if (exit_code) *exit_code = (int)(usize)retval;

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
