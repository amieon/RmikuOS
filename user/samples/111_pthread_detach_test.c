/* 111_pthread_detach_test.c —— pthread_detach 生命周期与自动回收回归
 *
 * 线程仍用 thread_create() 创建：pthread_create / exit / join 还没做，
 * 因为 void* 返回值过不了内核的 i32 exit_code。detach 对 thread_create
 * 建出来的线程同样有效，本测试就这么用。
 *
 * 覆盖：
 *   1. create 后 detach -> 线程照常跑完，join 被内核拒(-EINVAL)
 *   2. 重复 detach 幂等、worker 内 pthread_detach(pthread_self())
 *   3. 「先退出、后 detach」-> 内核当场补 reap，不留孤儿 TCB
 *   4. 批量 detach：栈记录表不涨、tid 被复用
 *
 * 第 4 条的判据是这里最关键的一条：alloc_tid() 从 threads Vec 的
 * 第一个空槽发号，detached 线程被 reap 后槽空出来，下一轮 create 会
 * 拿到同一个 tid。若内核只打标志、不回收，tid 会严格递增 —— 这条断言
 * 能直接抓住「编译过了，但 detach 了照样泄漏」。
 * 末尾还打印首尾两轮的栈基址：相等说明内核连 VA 也 dealloc 回去了，
 * 但那依赖分配器当时的空闲布局，只作观测、不计入断言，避免假红。
 */
#include "user.h"
#include "pthread.h"
#include "thread.h"

#define N_ROUND      12
#define YIELD_WAIT   4000

static int g_failures;
static volatile int g_done;
static volatile int g_self_detach_rc;

/* 项目 puts 自带换行(stdio.h)，拼一行只能用 fputs。 */
static void check(int cond, const char *what) {
    if (cond) {
        fputs("  ok   : ", stdout);
    } else {
        fputs("  FAIL : ", stdout);
        g_failures++;
    }
    fputs(what, stdout);
    fputc('\n', stdout);
}

static void put_hex(usize v) {
    static const char digits[] = "0123456789abcdef";
    fputs("0x", stdout);
    for (int i = (int)(sizeof(usize) * 2) - 1; i >= 0; i--) {
        fputc(digits[(v >> (i * 4)) & 0xf], stdout);
    }
}

static void yield_rounds(int n) {
    for (int i = 0; i < n; i++) RK_PTHREAD_YIELD();
}

/* done 只说明 worker 已经写了标志，后面还要跑 destructor + SYS_THREAD_EXIT
 * 再等属主核 back-from 才真正 reap。多给几轮 yield 让这条链走完。 */
static void wait_and_settle(void) {
    for (int i = 0; i < YIELD_WAIT && !g_done; i++) RK_PTHREAD_YIELD();
    yield_rounds(YIELD_WAIT);
}

static void plain_worker(void *arg) {
    (void)arg;
    g_done = 1;
}

static void self_detach_worker(void *arg) {
    (void)arg;
    g_self_detach_rc = pthread_detach(pthread_self());
    g_done = 1;
}

static int count_records(void) {
    int n = 0;
    for (int i = 0; i < THREAD_MAX_RECORDS; i++) {
        if (thread_stack_records[i].used) n++;
    }
    return n;
}

int main(void) {
    int baseline = count_records();
    int code = 0;

    fputs("== pthread_detach regression ==\n", stdout);

    /* ---- 1. 常规：创建后立刻 detach ---- */
    g_done = 0;
    int t1 = thread_create(plain_worker, 0);
    check(t1 > 0, "phase1 thread_create");
    check(pthread_detach((pthread_t)t1) == 0, "phase1 pthread_detach returns 0");
    /* 线程还活着时重复 detach：内核 already_detached 分支直接给 0。 */
    check(pthread_detach((pthread_t)t1) == 0, "phase1 repeated detach is idempotent");
    wait_and_settle();
    check(g_done == 1, "phase1 detached worker still ran to completion");
    check(thread_join(t1, &code) < 0, "phase1 join(detached) rejected by kernel");
    check(count_records() == baseline, "phase1 stack record taken by detach");

    /* ---- 2. worker 自己 detach 自己 ---- */
    g_done = 0;
    g_self_detach_rc = -999;
    int t2 = thread_create(self_detach_worker, 0);
    check(t2 > 0, "phase2 thread_create");
    wait_and_settle();
    check(g_done == 1, "phase2 self-detach worker ran");
    check(g_self_detach_rc == 0, "phase2 pthread_detach(pthread_self()) == 0");
    check(thread_join(t2, &code) < 0, "phase2 join(self-detached) rejected");

    /* ---- 3. 先退出、后 detach：back-from 已经错过，内核要当场补账 ---- */
    g_done = 0;
    int t3 = thread_create(plain_worker, 0);
    check(t3 > 0, "phase3 thread_create");
    wait_and_settle();   /* 此时是 joinable Zombie，不会被自动 reap */
    check(pthread_detach((pthread_t)t3) == 0, "phase3 detach after exit returns 0");
    check(thread_join(t3, &code) < 0, "phase3 join after late detach rejected");
    check(count_records() == baseline, "phase3 late detach still frees record");

    /* ---- 4. 批量：表不涨 + tid 复用(= 内核真的 reap 了 TCB) ---- */
    int first_tid = -1;
    int reused = 0;
    void *first_base = 0;
    void *last_base = 0;

    for (int r = 0; r < N_ROUND; r++) {
        g_done = 0;
        int tid = thread_create(plain_worker, 0);
        if (tid < 0) {
            check(0, "phase4 thread_create");
            break;
        }

        void *base = 0;
        usize size = 0;
        thread_stack_record_peek(tid, &base, &size);
        if (r == 0) {
            first_tid = tid;
            first_base = base;
        } else if (tid == first_tid) {
            reused++;
        }
        last_base = base;

        if (pthread_detach((pthread_t)tid) != 0) {
            check(0, "phase4 pthread_detach");
            break;
        }
        wait_and_settle();
    }

    check(count_records() == baseline, "phase4 stack record table does not grow");
    check(reused > 0, "phase4 tid reused => kernel reaped detached TCBs");

    fputs("  info : first stack base ", stdout);
    put_hex((usize)first_base);
    fputs(" / last ", stdout);
    put_hex((usize)last_base);
    fputc('\n', stdout);

    if (g_failures) {
        fputs("pthread_detach_test FAIL\n", stdout);
        return 1;
    }
    fputs("pthread_detach_test PASS\n", stdout);
    return 0;
}
