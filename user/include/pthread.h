#pragma once
#ifndef _RMIKU_PTHREAD_H
#define _RMIKU_PTHREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lock.h"     /* spinlock_t / __sync_* 的 TCC 声明 */
#include "arch.h"     /* get_epoch_us() */
#include "errno.h"

/* 结构体 timespec 只给 timedlock/timedwait 用。宿主差分测试可以定义
 * RK_PTHREAD_NO_TIMESPEC 自己提供，避免把整个 time.h -> stdio.h 链拉进来。 */
#ifndef RK_PTHREAD_NO_TIMESPEC
#include "time.h"
#endif

/* 三个可注入的原语：真机走 syscall，宿主差分测试可以替换成自己的实现。
 * 这是本文件能在宿主直接编译运行的关键，不要删。 */
#ifndef RK_PTHREAD_TID
#define RK_PTHREAD_TID() ((int)syscall3(SYS_GETTID, 0, 0, 0))
#endif
#ifndef RK_PTHREAD_YIELD
#define RK_PTHREAD_YIELD() ((void)syscall3(SYS_YIELD, 0, 0, 0))
#endif
#ifndef RK_PTHREAD_NOW_US
/* POSIX 默认 cond clock 是 CLOCK_REALTIME，abstime 也按墙钟给，所以这里用
 * epoch 微秒而不是单调钟。开机 NTP 校时会跳变；真机只在开机校一次，
 * 运行期稳定。要严格单调得让 condattr_setclock 真正生效，属于后续。 */
#define RK_PTHREAD_NOW_US() ((isize)get_epoch_us())
#endif

/*
 * 本版只提供「纯用户态能正确实现」的同步原语：
 *   mutex（含递归）/ cond / once / attr 空桩 / self / equal
 * 不提供 pthread_create / exit / join / detach、TLS、rwlock、spin、barrier、
 * 
 * pthread_kill / pthread_sigmask 已在 signal.h，需要就一起 include。
 *
 * cond_wait 用「记 seq -> 放锁 -> yield 轮询 seq 变化」实现。功能正确，
 * 代价是等待期间忙等（每次让出 CPU），不是阻塞睡眠
 */

/* ---- 类型与常量 ---- */
typedef unsigned long pthread_t;

typedef struct { unsigned long opaque[8]; } pthread_attr_t;
typedef struct { int type; int pshared; } pthread_mutexattr_t;
typedef struct { int pshared; int clock_id; } pthread_condattr_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_SCOPE_SYSTEM    0
#define PTHREAD_SCOPE_PROCESS   1
#define PTHREAD_INHERIT_SCHED   0
#define PTHREAD_EXPLICIT_SCHED  1
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED  1
#define PTHREAD_PRIO_NONE       0
#define PTHREAD_PRIO_INHERIT    1
#define PTHREAD_PRIO_PROTECT    2
#define PTHREAD_MUTEX_STALLED   0
#define PTHREAD_MUTEX_ROBUST    1

#define PTHREAD_MUTEX_NORMAL      0
#define PTHREAD_MUTEX_RECURSIVE   1
#define PTHREAD_MUTEX_ERRORCHECK  2
#define PTHREAD_MUTEX_DEFAULT     PTHREAD_MUTEX_NORMAL

typedef struct {
    volatile int locked;
    volatile int owner;   /* 持有者 tid，-1 表示无人持有 */
    unsigned depth;
    int type;
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER {0, -1, 0, PTHREAD_MUTEX_DEFAULT}

typedef struct {
    volatile unsigned seq;   /* signal/broadcast 时单调 +1，永不清零 */
    spinlock_t guard;
    unsigned waiters;
} pthread_cond_t;

#define PTHREAD_COND_INITIALIZER {0, {0}, 0}

typedef struct {
    volatile int state;      /* 0 未跑 / 1 正在跑 / 2 完成 */
    spinlock_t guard;
} pthread_once_t;

#define PTHREAD_ONCE_INIT {0, {0}}

/* ---- self / equal ---- */
static inline pthread_t pthread_self(void) {
    return (pthread_t)(usize)RK_PTHREAD_TID();
}

static inline int pthread_equal(pthread_t a, pthread_t b) {
    return a == b;
}

/* ---- mutex ---- */
static inline int pthread_mutex_init(
    pthread_mutex_t *m, const pthread_mutexattr_t *a
) {
    if (!m) return EINVAL;
    m->locked = 0;
    m->owner  = -1;
    m->depth  = 0;
    m->type   = a ? a->type : PTHREAD_MUTEX_DEFAULT;
    __sync_synchronize();
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *m) {
    if (!m) return EINVAL;
    return m->locked ? EBUSY : 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *m) {
    if (!m) return EINVAL;
    int me = RK_PTHREAD_TID();

    /* 递归快路径。安全前提：unlock 一定先写 owner=-1 再放锁，
     * 否则「别人刚拿到锁、owner 还是我」会被误判成递归，两个线程同进临界区。 */
    if (m->type != PTHREAD_MUTEX_NORMAL && m->owner == me && m->locked) {
        if (m->type == PTHREAD_MUTEX_RECURSIVE) {
            m->depth++;
            return 0;
        }
        return EDEADLK;
    }

    while (__sync_lock_test_and_set(&m->locked, 1)) {
        RK_PTHREAD_YIELD();
    }
    __sync_synchronize();
    m->owner = me;
    m->depth = 1;
    return 0;
}

static inline int pthread_mutex_trylock(pthread_mutex_t *m) {
    if (!m) return EINVAL;
    int me = RK_PTHREAD_TID();

    if (m->type == PTHREAD_MUTEX_RECURSIVE && m->owner == me && m->locked) {
        m->depth++;
        return 0;
    }
    if (__sync_lock_test_and_set(&m->locked, 1)) return EBUSY;

    __sync_synchronize();
    m->owner = me;
    m->depth = 1;
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *m) {
    if (!m) return EINVAL;
    if (!m->locked) return EPERM;

    int me = RK_PTHREAD_TID();
    if (m->type != PTHREAD_MUTEX_NORMAL && m->owner != me) return EPERM;

    if (m->depth > 1) {
        m->depth--;
        return 0;
    }

    m->depth = 0;
    m->owner = -1;               /* 必须先清，见 lock 的递归快路径 */
    __sync_synchronize();
    __sync_lock_release(&m->locked);
    return 0;
}

static inline int pthread_mutex_timedlock(
    pthread_mutex_t *m, const struct timespec *abstime
) {
    if (!abstime) return pthread_mutex_lock(m);

    isize deadline = (isize)abstime->tv_sec * 1000000
                   + (isize)(abstime->tv_nsec / 1000);
    for (;;) {
        int r = pthread_mutex_trylock(m);
        if (r == 0) return 0;
        if (r != EBUSY) return r;
        if (RK_PTHREAD_NOW_US() >= deadline) return ETIMEDOUT;
        RK_PTHREAD_YIELD();
    }
}

/* ---- cond ----
 * wait 和 timedwait 共用这里，避免「两份实现改一处漏一处」。 */
static inline int _rk_cond_wait_until(
    pthread_cond_t *c,
    pthread_mutex_t *m,
    isize deadline,
    int timed
) {
    if (!c || !m) return EINVAL;

    /* 先登记等待意图并记下当前纪元，再放锁。POSIX 要求调用方持 m 且已确认
     * 谓词为假，signal 方也持 m 改谓词，所以 signal 不可能落在「读 seq」之前
     * —— 这就是不丢唤醒的根据。反过来若不先登记就放锁，就会经典丢唤醒。 */
    spin_lock(&c->guard);
    unsigned seq = c->seq;
    c->waiters++;
    spin_unlock(&c->guard);

    int unlock_rc = pthread_mutex_unlock(m);
    if (unlock_rc != 0) return unlock_rc;

    int rc = 0;
    for (;;) {
        spin_lock(&c->guard);
        unsigned now = c->seq;
        spin_unlock(&c->guard);

        if (now != seq) break;
        if (timed && RK_PTHREAD_NOW_US() >= deadline) {
            rc = ETIMEDOUT;
            break;
        }
        RK_PTHREAD_YIELD();
    }

    spin_lock(&c->guard);
    if (c->waiters > 0) c->waiters--;
    spin_unlock(&c->guard);

    /* POSIX：timedwait 超时返回时也必须重新持有 mutex。 */
    int lock_rc = pthread_mutex_lock(m);
    return rc ? rc : lock_rc;
}

static inline int pthread_cond_init(
    pthread_cond_t *c, const pthread_condattr_t *a
) {
    if (!c) return EINVAL;
    c->seq = 0;
    c->waiters = 0;
    spin_init(&c->guard);
    (void)a;
    return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t *c) {
    if (!c) return EINVAL;
    return c->waiters ? EBUSY : 0;
}

static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    return _rk_cond_wait_until(c, m, 0, 0);
}

static inline int pthread_cond_timedwait(
    pthread_cond_t *c,
    pthread_mutex_t *m,
    const struct timespec *abstime
) {
    if (!abstime) return pthread_cond_wait(c, m);

    isize deadline = (isize)abstime->tv_sec * 1000000
                   + (isize)(abstime->tv_nsec / 1000);
    return _rk_cond_wait_until(c, m, deadline, 1);
}

/* signal 用全局 seq 会唤醒所有 waiter（惊群）。POSIX 允许 signal 唤醒
 * 多于一个线程、也允许虚假唤醒，所以语义正确，只是多几次空转。
 * 要精确单唤醒得给每个 waiter 发 ticket，属于优化，不是正确性要求。 */
static inline int pthread_cond_signal(pthread_cond_t *c) {
    if (!c) return EINVAL;
    spin_lock(&c->guard);
    c->seq++;
    spin_unlock(&c->guard);
    return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t *c) {
    return pthread_cond_signal(c);
}

/* ---- once ---- */
static inline int pthread_once(pthread_once_t *o, void (*init)(void)) {
    if (!o || !init) return EINVAL;

    for (;;) {
        spin_lock(&o->guard);
        int state = o->state;
        if (state == 0) o->state = 1;    /* 抢到初始化权 */
        spin_unlock(&o->guard);

        if (state == 2) return 0;
        if (state == 0) {
            init();
            spin_lock(&o->guard);
            o->state = 2;
            spin_unlock(&o->guard);
            __sync_synchronize();
            return 0;
        }
        RK_PTHREAD_YIELD();              /* 别的线程正在 init，等它完成 */
    }
}

/* ---- attr 空桩 ----
 * 除 settype/gettype 外全部只存不生效：本内核没有调度策略、guard page、
 * 进程间共享、robust 语义。返回 0 让移植代码能走完初始化流程。
 * settype 必须真存，sqlite3 的 init_recursive_mutex 靠它拿递归锁。 */
static inline int pthread_attr_init(pthread_attr_t *a) {
    if (!a) return EINVAL;
    return 0;
}
static inline int pthread_attr_destroy(pthread_attr_t *a) {
    (void)a; return 0;
}
static inline int pthread_attr_setdetachstate(pthread_attr_t *a, int v) {
    (void)a; (void)v; return 0;
}
static inline int pthread_attr_getdetachstate(const pthread_attr_t *a, int *v) {
    (void)a; if (v) *v = PTHREAD_CREATE_JOINABLE; return 0;
}
static inline int pthread_attr_setguardsize(pthread_attr_t *a, usize v) {
    (void)a; (void)v; return 0;
}
static inline int pthread_attr_getguardsize(const pthread_attr_t *a, usize *v) {
    (void)a; if (v) *v = 0; return 0;
}
static inline int pthread_attr_setstacksize(pthread_attr_t *a, usize v) {
    (void)a; (void)v; return 0;
}
static inline int pthread_attr_getstacksize(const pthread_attr_t *a, usize *v) {
    (void)a; if (v) *v = 64 * 1024; return 0;
}
static inline int pthread_attr_setstack(pthread_attr_t *a, void *p, usize n) {
    (void)a; (void)p; (void)n; return 0;
}
static inline int pthread_attr_getstack(const pthread_attr_t *a, void **p, usize *n) {
    (void)a; if (p) *p = 0; if (n) *n = 64 * 1024; return 0;
}
static inline int pthread_attr_setschedpolicy(pthread_attr_t *a, int v) {
    (void)a; (void)v; return 0;
}
static inline int pthread_attr_getschedpolicy(const pthread_attr_t *a, int *v) {
    (void)a; if (v) *v = 0; return 0;
}
static inline int pthread_attr_setinheritsched(pthread_attr_t *a, int v) {
    (void)a; (void)v; return 0;
}
static inline int pthread_attr_getinheritsched(const pthread_attr_t *a, int *v) {
    (void)a; if (v) *v = PTHREAD_INHERIT_SCHED; return 0;
}
static inline int pthread_attr_setscope(pthread_attr_t *a, int v) {
    (void)a; (void)v; return 0;
}
static inline int pthread_attr_getscope(const pthread_attr_t *a, int *v) {
    (void)a; if (v) *v = PTHREAD_SCOPE_SYSTEM; return 0;
}

static inline int pthread_mutexattr_init(pthread_mutexattr_t *a) {
    if (!a) return EINVAL;
    a->type = PTHREAD_MUTEX_DEFAULT;
    a->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}
static inline int pthread_mutexattr_destroy(pthread_mutexattr_t *a) {
    (void)a; return 0;
}
static inline int pthread_mutexattr_settype(pthread_mutexattr_t *a, int v) {
    if (!a) return EINVAL;
    if (v != PTHREAD_MUTEX_NORMAL && v != PTHREAD_MUTEX_RECURSIVE &&
        v != PTHREAD_MUTEX_ERRORCHECK) return EINVAL;
    a->type = v;
    return 0;
}
static inline int pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *v) {
    if (!a || !v) return EINVAL;
    *v = a->type;
    return 0;
}
static inline int pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int v) {
    if (!a) return EINVAL;
    a->pshared = v;
    return 0;
}
static inline int pthread_mutexattr_getpshared(const pthread_mutexattr_t *a, int *v) {
    if (!a || !v) return EINVAL;
    *v = a->pshared;
    return 0;
}
static inline int pthread_mutexattr_setprotocol(pthread_mutexattr_t *a, int v) {
    (void)a; (void)v; return 0;
}
static inline int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *a, int *v) {
    (void)a; if (v) *v = PTHREAD_PRIO_NONE; return 0;
}
static inline int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *a, int v) {
    (void)a; (void)v; return 0;
}
static inline int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *a, int *v) {
    (void)a; if (v) *v = 0; return 0;
}
static inline int pthread_mutexattr_setrobust(pthread_mutexattr_t *a, int v) {
    (void)a; (void)v; return 0;
}
static inline int pthread_mutexattr_getrobust(const pthread_mutexattr_t *a, int *v) {
    (void)a; if (v) *v = PTHREAD_MUTEX_STALLED; return 0;
}

static inline int pthread_condattr_init(pthread_condattr_t *a) {
    if (!a) return EINVAL;
    a->pshared = PTHREAD_PROCESS_PRIVATE;
    a->clock_id = 0;
    return 0;
}
static inline int pthread_condattr_destroy(pthread_condattr_t *a) {
    (void)a; return 0;
}
static inline int pthread_condattr_setpshared(pthread_condattr_t *a, int v) {
    if (!a) return EINVAL;
    a->pshared = v;
    return 0;
}
static inline int pthread_condattr_getpshared(const pthread_condattr_t *a, int *v) {
    if (!a || !v) return EINVAL;
    *v = a->pshared;
    return 0;
}
static inline int pthread_condattr_setclock(pthread_condattr_t *a, int v) {
    if (!a) return EINVAL;
    a->clock_id = v;
    return 0;
}
static inline int pthread_condattr_getclock(const pthread_condattr_t *a, int *v) {
    if (!a || !v) return EINVAL;
    *v = a->clock_id;
    return 0;
}

#ifdef __cplusplus
}
#endif
#endif
