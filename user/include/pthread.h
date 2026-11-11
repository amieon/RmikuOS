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

/* 混合等待：前 K 轮纯 volatile 读自旋、零 syscall，超过才进慢路径。
 * 理由：一次 SYS_YIELD / futex_wait 是完整的上下文切换往返（trap 保存
 * 70+ 寄存器、两把全局锁、换页表、flush_tlb），而自旋只要 3~5 条指令。
 * 高频短等（barrier）自旋必赢；秒级长等才该睡。K 用 sched_yield_bench
 * 量出的 us/轮 校准，0 = 关闭自旋直接进慢路径。 */
#ifndef RK_PTHREAD_SPIN_ROUNDS
#define RK_PTHREAD_SPIN_ROUNDS 64
#endif

/* 真机默认走 futex 130/131；宿主差分测试没有这两个号，定义
 * RK_PTHREAD_DISABLE_FUTEX 退回纯 yield 版。两版共用同一份结构体布局和
 * wait/timedwait 实现，对拍结果才有意义。 */
#ifdef RK_PTHREAD_DISABLE_FUTEX
#define RK_PTHREAD_WAIT(addr, val) ((void)RK_PTHREAD_YIELD())
#define RK_PTHREAD_WAKE(addr, n)   ((void)0)
#else
#define RK_PTHREAD_WAIT(addr, val) ((void)rk_futex_wait((addr), (unsigned)(val)))
#define RK_PTHREAD_WAKE(addr, n)   ((void)rk_futex_wake((addr), (n)))
#endif
#define RK_PTHREAD_WAKE_ALL 0x7fffffff


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
    /* 慢路径登记数。unlock 只在它 >0 时发 futex_wake，
     * 否则无争用的 unlock 也会白付一次 syscall。 */
    volatile int waiters;
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER {0, -1, 0, PTHREAD_MUTEX_DEFAULT, 0}

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


typedef unsigned int pthread_key_t;

#define PTHREAD_KEYS_MAX 128
#define PTHREAD_DESTRUCTOR_ITERATIONS 4

#ifndef RK_PTHREAD_TSD_GET
#define RK_PTHREAD_TSD_GET(k) \
    ((isize)syscall3(SYS_TSD_GET, (usize)(k), 0, 0))
#endif
#ifndef RK_PTHREAD_TSD_SET
#define RK_PTHREAD_TSD_SET(k, v) \
    ((isize)syscall3(SYS_TSD_SET, (usize)(k), (usize)(v), 0))
#endif

#ifndef RK_PTHREAD_DETACH
#define RK_PTHREAD_DETACH(tid, base, size) \
    ((isize)syscall3(SYS_THREAD_DETACH, (usize)(tid), (usize)(base), (usize)(size)))
#endif


int  pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int  pthread_setspecific(pthread_key_t key, const void *value);
void *pthread_getspecific(pthread_key_t key);
int  pthread_key_delete(pthread_key_t key);

/* 非 POSIX 内部钩子：__thread_entry 与 exit() 在退出前调用，
 * 跑 PTHREAD_DESTRUCTOR_ITERATIONS 轮 destructor。 */
void rk_tsd_run_destructors(void);

int pthread_detach(pthread_t thread);

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
    m->waiters = 0;
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

    /* 前 K 轮纯自旋：短争用根本不该进内核。先 volatile 读再 CAS，
     * 自旋段不做原子写，避免多核对同一 cache line 反复 ping-pong。 */
    unsigned spins = RK_PTHREAD_SPIN_ROUNDS;
    for (;;) {
        if (!m->locked && !__sync_lock_test_and_set(&m->locked, 1)) break;
        if (spins) { spins--; continue; }

        __sync_fetch_and_add(&m->waiters, 1);
        RK_PTHREAD_WAIT(&m->locked, 1);
        __sync_fetch_and_add(&m->waiters, -1);
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
    /* release 必须在读 waiters 之前：晚到的 waiter 进内核会读到 locked==0，
     * 拿 -EAGAIN 立刻回来重试，不会睡在没人 wake 的队列上。
     * 只叫醒 1 个 —— 锁本来也只能给一个，其余由后续 unlock 逐个唤醒。 */
    if (m->waiters) RK_PTHREAD_WAKE(&m->locked, 1);
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
        /* 这里必须留 YIELD 不能换 WAIT：futex_wait 第一版不带超时，
         * 睡下去就没人按 deadline 叫醒了。 */
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
    unsigned spins = RK_PTHREAD_SPIN_ROUNDS;
    for (;;) {
        /* 纯 volatile 读：不抢 guard、不查时钟、零 syscall。seq 单调递增，
         * 多转一轮没有正确性代价（POSIX 允许虚假唤醒）。 */
        unsigned now = c->seq;
        if (now != seq) break;
        if (spins) { spins--; continue; }

        if (timed) {
            /* NOW_US() 本身是 SYS 129，必须等自旋段跑完再查，否则
             * 「前 K 轮零 syscall」当场破功；代价是最多晚 K 轮发现超时。 */
            if (RK_PTHREAD_NOW_US() >= deadline) { rc = ETIMEDOUT; break; }
            RK_PTHREAD_YIELD();   /* timedwait 不能睡进无超时的 futex */
        } else {
            /* -EAGAIN = seq 已变，下一轮循环就退出；0 = 被定向唤醒。
             * signal 先 seq++ 再 wake，内核读值与挂队列同锁，三个落点都不丢。 */
            RK_PTHREAD_WAIT(&c->seq, seq);
        }
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

/* seq 是全局单调计数器、没有 waiter 世代分组，所以 signal 和 broadcast
 * 都唤醒全部：只 wake 1 个的话，其余已 block 的 waiter 记的是旧 seq，
 * 等待意图会悬空（glibc 靠 G1/G2 两组队列才敢精确单唤醒）。
 * POSIX 允许 signal 唤醒多于一个、也允许虚假唤醒，语义正确。
 * waiters>0 才发 syscall；signal 方按约定持 m，wait 方在放 m 之前已登记，
 * 所以 w==0 时确实没有需要叫醒的人。 */
static inline int pthread_cond_signal(pthread_cond_t *c) {
    if (!c) return EINVAL;
    spin_lock(&c->guard);
    c->seq++;
    unsigned w = c->waiters;
    spin_unlock(&c->guard);
    if (w) RK_PTHREAD_WAKE(&c->seq, RK_PTHREAD_WAKE_ALL);
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
        /* 别的线程正在 init，等它完成。once 只在初始化窗口等一次，state
         * 又没有 waiter 门控，保持 YIELD 比为此再铺一对 futex 更简单。 */
        RK_PTHREAD_YIELD();
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
