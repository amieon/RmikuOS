/* thread.c —— 必须跨 TU 只有一份的线程态：栈记录表 + TSD key 表 + errno 槽。
 *
 * thread.h 是 header-only, 绝大多数函数是 static inline。唯独这两张表和它们的锁
 * 不能留在头文件里: static 在头里等于每个包含它的 TU 各得一份(512 × 24B = 12 KiB
 * .bss + 一把各管各的锁)。后果不是崩溃而是静默失效 ——
 *   在 A.c 里 thread_create、在 B.c 里 thread_join 时, B.c 的表里没有这个 tid,
 *   thread_stack_record_take 返回 -1, thread_join 跳过 munmap, 每次泄漏 64 KiB 栈,
 *   不报错、不崩、跑久了才崩。TSD key 表同理: A.c create 的 key 在 B.c 查不到。
 *
 * errno 的 once / key / fallback 同样落在本文件。这个 .c 必须参与链接
 * (build.py 里作为 libc 核心件之一, 和 string.c 并列)。
 *
 * 逻辑也一并搬到这里, 让"表和操作表的函数"待在同一个 TU, 不必在每个 TU 重复 emit。
 * 它依赖 lock.h 的 spinlock, 但本文件不会定义 exit/memset 之类, 与 string.c 无冲突。
 */
#include "../include/thread.h"
#include "../include/pthread.h"
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

/* peek：查记录但不摘。pthread_detach 必须先 peek 到 (base,size) 交给内核，
 * syscall 成功后才 take；反过来先 take 再发 syscall，一旦失败记录就丢了，
 * 这条线程既没人 join 也没人 unmap，64 KiB 栈永久泄漏。 */
int thread_stack_record_peek(
    int tid,
    void **stack_base,
    usize *stack_size
) {
    mutex_lock(&thread_stack_lock);

    for (int i = 0; i < THREAD_MAX_RECORDS; i++) {
        if (thread_stack_records[i].used &&
            thread_stack_records[i].tid == tid) {
            if (stack_base) *stack_base = thread_stack_records[i].stack_base;
            if (stack_size) *stack_size = thread_stack_records[i].stack_size;

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

/* 
 * 分离态不是用户态一张表能做完的事：内核 reap_thread() 全仓只有
 * thread_join_current() 一个调用点，没人 join 的线程 TCB 永远不回收；
 * 而 64 KiB 用户栈也只有 thread_join() 会 munmap。所以内核侧要
 * (1) TCB 打 detached 标志，(2) 在 run_tasks back-from 自动 reap，
 * (3) 顺带把这里登记的 (base,size) 从进程地址空间 remove_area 掉。
 *
 * 退出线程自己不能 munmap 自己的栈 —— 它正踩在上面。这里由创建者
 * (或任意知道 tid 的同进程线程) 把栈地址交给内核，是最短的路径。
 *
 * 允许「先退出、后 detach」：内核发现目标已 Zombie 且归属已释放时会
 * 当场补 reap + unmap，不会留下永远没人回收的孤儿。
 */
int pthread_detach(pthread_t thread) {
    int tid = (int)(usize)thread;

    void *stack_base = 0;
    usize stack_size = 0;
    /* 没有记录 = 不是本库 thread_create() 建的线程(或已经摘过表)。
     * 仍然发 syscall，只是传 0/0：内核只回收 TCB，不动栈。 */
    int have_stack = (thread_stack_record_peek(tid, &stack_base, &stack_size) == 0);

    isize r = RK_PTHREAD_DETACH(
        tid,
        have_stack ? (usize)stack_base : 0,
        have_stack ? stack_size : 0
    );
    if (r < 0) return (int)(-r);   /* pthread_* 不设 errno，返正数错误码 */

    /* 成功之后这段栈归内核管：reap 时它自己 remove_area。
     * 这里只摘表，绝不 munmap —— joinable 路径才由 thread_join() munmap。 */
    if (have_stack) {
        void *base = 0;
        usize size = 0;
        thread_stack_record_take(tid, &base, &size);
    }
    return 0;
}


struct tsd_key {
    volatile int in_use;
    void (*destructor)(void *);
};

static struct tsd_key tsd_keys[PTHREAD_KEYS_MAX];
static mutex_t tsd_key_lock = MUTEX_INIT;
static unsigned int tsd_next_key = 0;


static void *rk_tsd_get_slot(pthread_key_t key) {
    return (void *)(usize)RK_PTHREAD_TSD_GET(key);
}

static int rk_tsd_set_slot(pthread_key_t key, const void *value) {
    return (int)RK_PTHREAD_TSD_SET(key, value);
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    if (!key) return EINVAL;

    mutex_lock(&tsd_key_lock);

    /* 从 tsd_next_key 起单调找空槽，而不是永远从 0 扫：key_delete 之后
     * 立刻复用同一个号，老线程残留的旧值会被新用途 getspecific 读到。
     * 单调分配要绕满 128 个才可能复用，把坑压到几乎碰不到。 */
    unsigned int chosen = PTHREAD_KEYS_MAX;
    for (unsigned int n = 0; n < PTHREAD_KEYS_MAX; n++) {
        unsigned int cand = (tsd_next_key + n) % PTHREAD_KEYS_MAX;
        if (!tsd_keys[cand].in_use) {
            chosen = cand;
            break;
        }
    }
    if (chosen == PTHREAD_KEYS_MAX) {
        mutex_unlock(&tsd_key_lock);
        return EAGAIN;
    }

    tsd_keys[chosen].in_use = 1;
    tsd_keys[chosen].destructor = destructor;
    tsd_next_key = (chosen + 1) % PTHREAD_KEYS_MAX;
    *key = (pthread_key_t)chosen;

    mutex_unlock(&tsd_key_lock);

    /* 清掉当前线程这一槽里可能残留的孤儿值。别的线程残留值 POSIX 归
     * 调用方负责(key_delete 不遍历线程)，这里不越界去动。 */
    rk_tsd_set_slot((pthread_key_t)chosen, 0);
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    /* in_use 是 volatile 单写多读，热路径不上锁；create/delete 才加锁。 */
    if (key >= PTHREAD_KEYS_MAX || !tsd_keys[key].in_use) return EINVAL;
    if (rk_tsd_set_slot(key, value) != 0) return EINVAL;
    return 0;
}

void *pthread_getspecific(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX || !tsd_keys[key].in_use) return 0;
    return rk_tsd_get_slot(key);
}

int pthread_key_delete(pthread_key_t key) {
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;

    mutex_lock(&tsd_key_lock);
    if (!tsd_keys[key].in_use) {
        mutex_unlock(&tsd_key_lock);
        return EINVAL;
    }
    tsd_keys[key].in_use = 0;
    tsd_keys[key].destructor = 0;
    mutex_unlock(&tsd_key_lock);

    /* POSIX: 不调 destructor、不动任何线程已有的值 —— 那些指针由调用方负责。 */
    return 0;
}

/* 内部钩子：__thread_entry 与 exit() 在退出前各调一次。
 * POSIX 规定最多 PTHREAD_DESTRUCTOR_ITERATIONS 轮，且每轮必须先把值
 * 清成 NULL 再调 dtor，否则 destructor 里再 setspecific 会无限循环。
 * 没有 key 时整轮一个 syscall 都不发。 */
void rk_tsd_run_destructors(void) {
    for (int round = 0; round < PTHREAD_DESTRUCTOR_ITERATIONS; round++) {
        int again = 0;
        for (int i = 0; i < PTHREAD_KEYS_MAX; i++) {
            if (!tsd_keys[i].in_use) continue;
            void (*dtor)(void *) = tsd_keys[i].destructor;
            if (!dtor) continue;

            void *v = rk_tsd_get_slot((pthread_key_t)i);
            if (!v) continue;

            again = 1;
            rk_tsd_set_slot((pthread_key_t)i, 0);
            dtor(v);
        }
        if (!again) break;
    }
}

/* 
 * POSIX 要求 cleanup handler 在 pthread_exit（以及将来的 cancel）时按
 * LIFO 全部执行。节点在调用方栈帧上（push/pop 是宏，强制同一词法
 * 作用域），链表头则每线程一份、存在 TSD 槽里 —— 不必等 tp 归还用户态，
 * 也不依赖 __thread
 */
static pthread_once_t cleanup_once = PTHREAD_ONCE_INIT;
static pthread_key_t  cleanup_key;
static volatile int   cleanup_ready;

static void cleanup_init_key(void) {
    if (pthread_key_create(&cleanup_key, 0) == 0) cleanup_ready = 1;
}

static void cleanup_ensure(void) {
    if (!cleanup_ready) pthread_once(&cleanup_once, cleanup_init_key);
}

static struct rk_pthread_cleanup_node *cleanup_head(void) {
    cleanup_ensure();
    if (!cleanup_ready) return 0;
    return (struct rk_pthread_cleanup_node *)pthread_getspecific(cleanup_key);
}

static void cleanup_set_head(struct rk_pthread_cleanup_node *h) {
    cleanup_ensure();
    if (cleanup_ready) pthread_setspecific(cleanup_key, h);
}

void rk_pthread_cleanup_push(
    struct rk_pthread_cleanup_node *node,
    void (*routine)(void *),
    void *arg
) {
    if (!node || !routine) return;
    node->routine = routine;
    node->arg = arg;
    node->prev = cleanup_head();
    cleanup_set_head(node);
}

void rk_pthread_cleanup_pop(int execute) {
    struct rk_pthread_cleanup_node *h = cleanup_head();
    if (!h) return;
    /* 先摘链再调 routine：handler 里若又 push/pop，看到的是正确的上层链。 */
    cleanup_set_head(h->prev);
    if (execute) h->routine(h->arg);
}

/* pthread_exit 专用：链上尚未 pop 的 handler 全部按 LIFO 跑完。 */
static void cleanup_run_all(void) {
    struct rk_pthread_cleanup_node *h = cleanup_head();
    while (h) {
        struct rk_pthread_cleanup_node *prev = h->prev;
        cleanup_set_head(prev);
        h->routine(h->arg);
        h = prev;
    }
}

/* 
 * exec 只换 trap_cx、不重建 TCB，新镜像里 once/key 表归零而 TCB 槽还留着
 * 旧指针。这里用 pthread_key_create 领号，它内部的「清当前线程该槽」
 * 会把领到的那一槽抹成 0，errno 因此读回 NULL 走 fallback，
 * 不会把 exec 前的野指针当 int * 用。领到几号取决于首次访问 errno 的时机。
 */
static pthread_once_t errno_once = PTHREAD_ONCE_INIT;
static pthread_key_t  errno_key;
static volatile int   errno_ready;
static int            errno_fallback;

static void errno_init_key(void) {
    if (pthread_key_create(&errno_key, 0) == 0) errno_ready = 1;
}

int *rk_errno_slot(void) {
    if (!errno_ready) {
        pthread_once(&errno_once, errno_init_key);
        if (!errno_ready) return &errno_fallback;   /* 128 个 key 全满 */
    }
    int *p = (int *)pthread_getspecific(errno_key);
    return p ? p : &errno_fallback;
}

/* 非 POSIX 内部钩子：__thread_entry 在 func() 之前调一次。 */
void rk_tsd_bind_errno_slot(int *slot) {
    if (!slot) return;
    pthread_once(&errno_once, errno_init_key);
    if (errno_ready) pthread_setspecific(errno_key, slot);
}

int pthread_create(
    pthread_t *thread,
    const pthread_attr_t *attr,
    void *(*start_routine)(void *),
    void *arg
) {
    if (!thread || !start_routine) return EINVAL;

    usize stack_size = PTHREAD_STACK_DEFAULT;
    int detached = 0;
    if (attr) {
        /* opaque[0] 由 attr_init 清零，setdetachstate 真写；opaque[1] 是
         * setstacksize 写的值，0 表示用默认栈。不经过 init 的 attr 是
         * 未定义用法，这里读到的就是调用方自己的垃圾。 */
        if (attr->opaque[1]) stack_size = (usize)attr->opaque[1];
        detached = (attr->opaque[0] == PTHREAD_CREATE_DETACHED);
    }

    /* 内核 release_thread_stack 按页圆整后比对 mmap_areas 的 (start,end)，
     * 这里先自己向上对齐，避免 size 与内核登记的区间对不上、栈漏回收。 */
    stack_size = (stack_size + 4095) & ~(usize)4095;
    if (stack_size < PTHREAD_STACK_MIN) return EINVAL;

    int tid = thread_spawn_with_entry(
        (usize)__pthread_entry,
        (usize)start_routine,
        (usize)arg,
        stack_size
    );
    if (tid < 0) return EAGAIN;

    *thread = (pthread_t)(usize)tid;

    /* attr 指定的分离态就是一次显式 detach。worker 有可能已经跑完退出，
     * 内核 thread_detach_current 的「先退出、后 detach」补账会当场
     * reap + 解除栈映射，不会留下孤儿 TCB / 64 KiB 栈。 */
    if (detached) return pthread_detach(*thread);
    return 0;
}

int pthread_join(pthread_t thread, void **retval) {
    int tid = (int)(usize)thread;
    if (tid == gettid()) return EDEADLK;

    isize r = sys_thread_join_raw(tid, retval);
    if (r < 0) return (int)(-r);   /* pthread_* 不设 errno，返正数错误码 */

    /* 与 thread.h 的 thread_join() 同一套栈回收。join 成功后内核
     * 已 reap TCB，用户态只负责 munmap 自己 mmap 的那段 VA；detached
     * 线程会被内核直接拒掉，走不到这里，不存在重复释放。 */
    void *base = 0;
    usize size = 0;
    if (thread_stack_record_take(tid, &base, &size) == 0 && base && size) {
        munmap(base, size);
    }
    return 0;
}

__attribute__((noreturn)) void pthread_exit(void *retval) {
    /* POSIX 规定的顺序：cleanup handler 先于 TSD destructor。 */
    cleanup_run_all();
    rk_tsd_run_destructors();
    sys_thread_exit_raw((usize)retval);
    for (;;) {}
}
