/* 用户态 pthread 同步原语回归：mutex / 递归锁 / cond / once / timedwait。
 * 线程仍用本 OS 的 thread_create(thread.h)，pthread.h 本版不提供 create/join。
 * 宿主差分版见 target/pthread_diff_test.c（与 winpthreads 对拍）。 */
#include "test.h"
#include "pthread.h"
#include "thread.h"

#define N_WORKER 4
#define N_INC    2000
#define N_PROD   2
#define PER_PROD 500

static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_counter;

static void inc_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < N_INC; i++) {
        pthread_mutex_lock(&g_m);
        g_counter++;
        pthread_mutex_unlock(&g_m);
    }
}

static pthread_mutex_t r_m = PTHREAD_MUTEX_INITIALIZER;
static volatile int r_depth_ok;
static volatile int r_other_busy;

/* 主线程已经持锁之后才创建本线程，所以一进来就该拿到 EBUSY。
 * 不用 r_go 忙等标志：单核下子线程先跑会占着 CPU，主线程永远没机会置位。 */
static void recur_worker(void *arg) {
    (void)arg;
    r_other_busy = (pthread_mutex_trylock(&r_m) == EBUSY) ? 1 : -1;
    if (r_other_busy == -1) pthread_mutex_unlock(&r_m);
}

static pthread_mutex_t c_m  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  c_cv = PTHREAD_COND_INITIALIZER;
static volatile int c_count;
static volatile int c_produced;
static volatile int c_consumed;
static volatile int c_done_prod;

static void cond_producer(void *arg) {
    (void)arg;
    for (int i = 0; i < PER_PROD; i++) {
        pthread_mutex_lock(&c_m);
        c_count++;
        c_produced++;
        pthread_cond_signal(&c_cv);
        pthread_mutex_unlock(&c_m);
    }
    pthread_mutex_lock(&c_m);
    c_done_prod++;
    pthread_cond_broadcast(&c_cv);
    pthread_mutex_unlock(&c_m);
}

static void cond_consumer(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&c_m);
        while (c_count == 0 && c_done_prod < N_PROD) {
            pthread_cond_wait(&c_cv, &c_m);
        }
        if (c_count == 0 && c_done_prod >= N_PROD) {
            pthread_mutex_unlock(&c_m);
            break;
        }
        c_count--;
        c_consumed++;
        pthread_mutex_unlock(&c_m);
    }
}

static pthread_once_t o_once = PTHREAD_ONCE_INIT;
static volatile int o_hits;

static void once_fn(void) { o_hits++; }

static void once_worker(void *arg) {
    (void)arg;
    pthread_once(&o_once, once_fn);
}

int main(void) {
    TEST_START("pthread_sync");

    /* 1. 互斥锁：4 线程各加 2000 次，总和必须精确 */
    int tids[N_WORKER];
    for (int i = 0; i < N_WORKER; i++) tids[i] = thread_create(inc_worker, 0);
    for (int i = 0; i < N_WORKER; i++) thread_join(tids[i], 0);
    CHECK_EQ(g_counter, N_WORKER * N_INC, "互斥锁保护下的并发计数精确");

    /* 2. 递归锁：同线程嵌套 3 次 + 别的线程 trylock 必须 EBUSY */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&r_m, &attr);
    int type = -1;
    pthread_mutexattr_gettype(&attr, &type);
    CHECK_EQ(type, PTHREAD_MUTEX_RECURSIVE, "mutexattr_settype/gettype 真的存住了 type");

    pthread_mutex_lock(&r_m);
    pthread_mutex_lock(&r_m);
    pthread_mutex_lock(&r_m);
    r_depth_ok = 1;
    int rt = thread_create(recur_worker, 0);
    thread_join(rt, 0);
    CHECK_EQ(r_other_busy, 1, "递归锁持有期间别的线程 trylock 返回 EBUSY");
    CHECK_EQ(r_depth_ok, 1, "同线程嵌套加锁不会自锁死");
    pthread_mutex_unlock(&r_m);
    pthread_mutex_unlock(&r_m);
    pthread_mutex_unlock(&r_m);
    CHECK_EQ(pthread_mutex_trylock(&r_m), 0, "三次 unlock 之后锁真正释放");
    pthread_mutex_unlock(&r_m);

    /* 3. 条件变量：2 生产者 / 2 消费者，不丢唤醒、不重复消费 */
    int pt[N_PROD], ct[N_PROD];
    for (int i = 0; i < N_PROD; i++) pt[i] = thread_create(cond_producer, 0);
    for (int i = 0; i < N_PROD; i++) ct[i] = thread_create(cond_consumer, 0);
    for (int i = 0; i < N_PROD; i++) thread_join(pt[i], 0);
    for (int i = 0; i < N_PROD; i++) thread_join(ct[i], 0);
    CHECK_EQ(c_produced, N_PROD * PER_PROD, "生产者全部完成");
    CHECK_EQ(c_consumed, N_PROD * PER_PROD, "消费者一次不漏、一次不重");
    CHECK_EQ(c_count, 0, "队列最终清空");

    /* 4. once：4 线程并发，init 只跑一次 */
    for (int i = 0; i < N_WORKER; i++) tids[i] = thread_create(once_worker, 0);
    for (int i = 0; i < N_WORKER; i++) thread_join(tids[i], 0);
    CHECK_EQ(o_hits, 1, "pthread_once 的 init 只执行一次");

    /* 5. timedwait 超时，且返回时仍持有 mutex */
    pthread_mutex_t m2 = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t  c2 = PTHREAD_COND_INITIALIZER;
    struct timespec ts;
    ts.tv_sec  = (long)(get_epoch_us() / 1000000) + 1;
    ts.tv_nsec = 0;
    pthread_mutex_lock(&m2);
    int rc = pthread_cond_timedwait(&c2, &m2, &ts);
    CHECK_EQ(rc, ETIMEDOUT, "无人 signal 时 timedwait 返回 ETIMEDOUT");
    CHECK(pthread_mutex_trylock(&m2) != 0, "timedwait 超时返回时仍持有 mutex");
    pthread_mutex_unlock(&m2);

    /* 6. self / equal */
    CHECK(pthread_equal(pthread_self(), pthread_self()), "pthread_self + pthread_equal 一致");
    CHECK_EQ(pthread_mutex_destroy(&m2), 0, "mutex_destroy 在已解锁时返回 0");

    TEST_END();
}
