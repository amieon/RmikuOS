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
