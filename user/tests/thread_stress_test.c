#include "test.h"

/*
 * 线程反复创建/回收压力测试:累计 TOTAL 次 create+join。
 *
 * 为什么不是"同时 600 个存活线程":
 *   每个线程的内核栈是 512 KiB(kernel/src/task/kernel_stack.rs:13), 而且要求
 *   **连续 128 个物理页**(alloc_contiguous_frames), 分配失败走的是
 *   .expect(...) -> panic, 不是返回错误码。同时 600 个就是 300 MiB 连续帧,
 *   一旦碎片化整个内核直接崩; 而且每个栈都要 512 KiB 清零, TCG 下极慢。
 *   所以这里保持 BATCH 个并发、累计 TOTAL 次的串行流水 —— 这条路径才是栈泄漏
 *   真正会暴露的地方: 每一轮都是 "mmap 64 KiB -> 用 -> munmap 64 KiB"。
 *
 * 两个断言值得盯:
 *   1. 栈基址复用 —— join 里的 munmap 一旦没执行(比如表退回 per-TU, 查不到
 *      记录), 地址会一路往后涨, 唯一基址数顶到 MAX_TRACK, 这条直接失败。
 *   2. 表回到基线 —— 累计次数远超 THREAD_MAX_RECORDS(512), 只要 join 都正常
 *      摘记录, 每轮结束表都应为空, 永远走不到 "stack record table full"。
 */

#ifndef THREAD_STRESS_TOTAL
#define THREAD_STRESS_TOTAL 600
#endif

#ifndef THREAD_STRESS_BATCH
#define THREAD_STRESS_BATCH 8
#endif

/* 唯一栈基址的跟踪表。真复用的话只会剩少数几个地址。 */
#define MAX_TRACK 64
static usize g_seen[MAX_TRACK];
static int   g_seen_n;

static void track(usize v) {
    for (int i = 0; i < g_seen_n && i < MAX_TRACK; i++) {
        if (g_seen[i] == v) return;
    }

    if (g_seen_n < MAX_TRACK) {
        g_seen[g_seen_n++] = v;
    } else {
        g_seen_n = MAX_TRACK + 1;   /* 溢出: 说明地址没复用, 用来让断言失败 */
    }
}

static int g_slot[THREAD_STRESS_BATCH];

static void stress_worker(void *arg) {
    int i = (int)(isize)arg;
    g_slot[i] = i * 3 + 1;
    thread_exit(i);
}

/* ---------------- 并发互斥:多线程抢一把锁做非原子自增 ----------------
 * samples/23_thread_malloc_test.c 有同型用例(6×2000), 但它在 samples 里,
 * 不进回归。这里缩到 8×200 把它并进自动测试, 顺带验证锁内 yield 时互斥
 * 仍然成立(mutex 是 yield 自旋, 持锁 yield 就是最刁钻的调度点)。
 */
#define MUTEX_THREADS 8
#define MUTEX_ITERS   200

static int     g_counter;
static mutex_t g_lock = MUTEX_INIT;

static void mutex_worker(void *arg) {
    (void)arg;

    for (int i = 0; i < MUTEX_ITERS; i++) {
        mutex_lock(&g_lock);

        int old = g_counter;
        if ((i & 15) == 0) yield();     /* 持锁让出: 放大竞争窗口 */
        g_counter = old + 1;

        mutex_unlock(&g_lock);
    }

    thread_exit(7);
}

static int record_find(int tid, void **base) {
    for (int i = 0; i < THREAD_MAX_RECORDS; i++) {
        if (thread_stack_records[i].used &&
            thread_stack_records[i].tid == tid) {
            if (base) *base = thread_stack_records[i].stack_base;
            return 0;
        }
    }
    return -1;
}

static int record_count(void) {
    int n = 0;
    for (int i = 0; i < THREAD_MAX_RECORDS; i++) {
        if (thread_stack_records[i].used) n++;
    }
    return n;
}

int main() {
    TEST_START("thread_stress");

    /* ---- 0. 并发互斥(先跑: 即使后面压力部分出问题, 这组结果也已落地) ---- */
    {
        g_counter = 0;

        int mtids[MUTEX_THREADS];
        int mok = 1;

        for (int i = 0; i < MUTEX_THREADS; i++) {
            mtids[i] = thread_create(mutex_worker, 0);
            if (mtids[i] < 0) mok = 0;
        }

        for (int i = 0; i < MUTEX_THREADS; i++) {
            if (mtids[i] < 0) continue;

            int c = -1;
            if (thread_join(mtids[i], &c) < 0 || c != 7) mok = 0;
        }

        CHECK(mok, "8 线程创建/join 全部成功");
        CHECK_EQ(g_counter, MUTEX_THREADS * MUTEX_ITERS,
                 "互斥计数正确(8x200, 持锁 yield 放大竞争)");
        CHECK_EQ(record_count(), 0, "互斥组结束后记录表为空");
    }

    int base_records = record_count();

    int created = 0;
    int create_fail = 0;
    int join_fail = 0;
    int code_bad = 0;
    int val_bad = 0;
    int rec_miss = 0;
    int leak_rounds = 0;
    int rounds = 0;

    int tids[THREAD_STRESS_BATCH];

    for (int done = 0; done < THREAD_STRESS_TOTAL; done += THREAD_STRESS_BATCH) {
        int n = THREAD_STRESS_TOTAL - done;
        if (n > THREAD_STRESS_BATCH) n = THREAD_STRESS_BATCH;
        rounds++;

        for (int i = 0; i < n; i++) g_slot[i] = -1;

        /* 先全部创建, 让并发数在批内达到 BATCH */
        for (int i = 0; i < n; i++) {
            tids[i] = thread_create(stress_worker, (void *)(isize)i);

            if (tids[i] < 0) {
                create_fail++;
                continue;
            }
            created++;

            void *bp = 0;
            if (record_find(tids[i], &bp) == 0) {
                if (bp) track((usize)bp);
            } else {
                rec_miss++;
            }
        }

        /* 再全部回收 */
        for (int i = 0; i < n; i++) {
            if (tids[i] < 0) continue;

            int code = -1;
            if (thread_join(tids[i], &code) < 0) {
                join_fail++;
                continue;
            }
            if (code != i) code_bad++;
            if (g_slot[i] != i * 3 + 1) val_bad++;
        }

        if (record_count() != base_records) leak_rounds++;
    }

    CHECK_EQ(created, THREAD_STRESS_TOTAL, "累计创建线程数达到目标");
    CHECK_EQ(create_fail, 0, "thread_create 无失败");
    CHECK_EQ(join_fail, 0, "thread_join 无失败");
    CHECK_EQ(code_bad, 0, "每个线程退出码正确");
    CHECK_EQ(val_bad, 0, "每个线程都执行到并写入自己的槽位");
    CHECK_EQ(rec_miss, 0, "每次创建都能在栈记录表里查到(跨 TU 表生效)");
    CHECK_EQ(leak_rounds, 0, "每轮 join 后记录表回到基线(不随轮次增长)");
    CHECK_EQ(record_count(), base_records, "全部结束后记录表干净");
    CHECK_LE(g_seen_n, 32, "栈基址被复用(唯一基址 <= 32, 说明 munmap 生效)");

    printf("[INFO] rounds=%d batch=%d unique_stacks=%d\n",
           rounds, THREAD_STRESS_BATCH,
           (g_seen_n > MAX_TRACK) ? -1 : g_seen_n);

    TEST_END();
}
