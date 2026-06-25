#pragma once

/*
 * lock.h —— 用户态自旋锁 / 互斥锁。
 *
 * 内容:
 *   - spinlock_t + spin_init / spin_lock / spin_unlock
 *   - mutex_t(= spinlock_t 的别名)+ mutex_init / mutex_lock / mutex_unlock
 *
 * 只依赖 syscall.h:自旋等待时直接通过 syscall3(SYS_YIELD, ...) 让出 CPU,
 * 不依赖 process.h,避免与 mem.h / thread.h 形成循环依赖
 * (mem.h 和 thread.h 都需要 lock.h)。
 */

#include "syscall.h"

typedef struct {
    volatile int locked;
} spinlock_t;

#define SPINLOCK_INIT {0}

static inline void spin_init(spinlock_t *lk) {
    lk->locked = 0;
}

static inline void spin_lock(spinlock_t *lk) {
    while (__sync_lock_test_and_set(&lk->locked, 1)) {
        /* 自旋等待时让出 CPU(等价于 yield()) */
        syscall3(SYS_YIELD, 0, 0, 0);
    }

    /* full barrier:防止临界区内存访问被重排到 lock 前面。 */
    __sync_synchronize();
}

static inline void spin_unlock(spinlock_t *lk) {
    /* full barrier:防止临界区内存访问被重排到 unlock 后面。 */
    __sync_synchronize();
    __sync_lock_release(&lk->locked);
}

typedef spinlock_t mutex_t;

#define MUTEX_INIT SPINLOCK_INIT

static inline void mutex_init(mutex_t *m) {
    spin_init(m);
}

static inline void mutex_lock(mutex_t *m) {
    spin_lock(m);
}

static inline void mutex_unlock(mutex_t *m) {
    spin_unlock(m);
}
