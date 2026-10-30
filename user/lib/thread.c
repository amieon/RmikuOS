/* thread.c —— 线程栈记录表的唯一定义。
 *
 * thread.h 是 header-only, 绝大多数函数是 static inline。唯独这张表和它的锁不能
 * 留在头文件里: static 在头里等于每个包含它的 TU 各得一份(512 × 24B = 12 KiB
 * .bss + 一把各管各的锁)。后果不是崩溃而是静默失效 ——
 *   在 A.c 里 thread_create、在 B.c 里 thread_join 时, B.c 的表里没有这个 tid,
 *   thread_stack_record_take 返回 -1, thread_join 跳过 munmap, 每次泄漏 64 KiB 栈,
 *   不报错、不崩、跑久了才崩。
 *
 * 与 errno 同类问题: "必须只有一份"的东西不能放头文件。这个 .c 必须参与链接
 * (build.py 里作为 libc 核心件之一, 和 string.c 并列)。
 *
 * 逻辑也一并搬到这里, 让"表和操作表的函数"待在同一个 TU, 不必在每个 TU 重复 emit。
 * 它依赖 lock.h 的 spinlock, 但本文件不会定义 exit/memset 之类, 与 string.c 无冲突。
 */
#include "../include/thread.h"

struct thread_stack_record thread_stack_records[THREAD_MAX_RECORDS];
mutex_t thread_stack_lock = MUTEX_INIT;

int thread_stack_record_add(
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

int thread_stack_record_take(
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
