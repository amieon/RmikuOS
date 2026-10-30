#include "test.h"

/*
 * 用户态线程语义:create / join / 栈记录表 / 错误路径 / 退出码边界
 *
 * 表 thread_stack_records 定义在 lib/thread.c, 本文件是另一个 TU。这里能查到
 * 刚创建的 tid, 就是"表跨 TU 唯一"的现场证据 —— 若有人把它改回头文件里的
 * static, 下面几条会立刻失败, 而不是等内存慢慢漏光(那种漏法不崩不报错)。
 *
 * 只读查询走 record_find(), 不摘记录; 摘记录是 thread_join 里的事。
 */

#define NTHREADS    4
#define STACK_PAGE  4096

/* ---------------- 被创建的线程 ---------------- */

static int thread_vals[NTHREADS];

static void worker(void *arg) {
    int id = (int)(isize)arg;
    thread_vals[id] = id * 10 + 1;   /* 每个线程写自己的槽位 */
    thread_exit(id + 100);           /* RmikuOS 的 thread_exit 直接收 int 退出码 */
}

static int g_tid_seen = -1;

static void worker_tid(void *arg) {
    (void)arg;
    g_tid_seen = gettid();
}

static void worker_code(void *arg) {
    thread_exit((int)(isize)arg);
}

/* 16 KiB 栈帧:验证 64 KiB 线程栈放得下大局部数组 */
#define BIG_N 4096
static int g_big_sum = -1;

static void worker_bigstack(void *arg) {
    volatile int local[BIG_N];
    (void)arg;

    for (int i = 0; i < BIG_N; i++) local[i] = i & 0x7f;

    int s = 0;
    for (int i = 0; i < BIG_N; i++) s += local[i];

    g_big_sum = s;
}

static int g_alloc_ok = -1;

static void worker_alloc(void *arg) {
    (void)arg;

    char *p = (char *)malloc(256);
    if (!p) {
        g_alloc_ok = -1;
        return;
    }

    p[0] = 'x';
    p[255] = 'y';
    g_alloc_ok = (p[0] == 'x' && p[255] == 'y') ? 1 : 0;
    free(p);
}

/* ---------------- 表查询辅助 ---------------- */

static int record_find(int tid, void **base, usize *size) {
    for (int i = 0; i < THREAD_MAX_RECORDS; i++) {
        if (thread_stack_records[i].used &&
            thread_stack_records[i].tid == tid) {
            if (base) *base = thread_stack_records[i].stack_base;
            if (size) *size = thread_stack_records[i].stack_size;
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

/* ---------------- 测试主体 ---------------- */

int main() {
    TEST_START("thread_basic");

    int base_records = record_count();   /* 主线程自身不该占记录 */
    CHECK_EQ(base_records, 0, "起始状态栈记录表为空");

    /* ---- 1. create / 参数 / 退出码 ---- */
    int tids[NTHREADS];
    int ok = 1;
    for (int i = 0; i < NTHREADS; i++) {
        thread_vals[i] = -1;
        tids[i] = thread_create(worker, (void *)(isize)i);
        if (tids[i] < 0) ok = 0;
    }
    CHECK(ok, "thread_create 全部成功");

    /* ---- 2. 栈记录表:跨 TU 可见 + 内容正确 ---- */
    void *base0 = 0;
    usize size0 = 0;
    int found = record_find(tids[0], &base0, &size0);

    CHECK_EQ(found, 0, "栈记录表跨 TU 可见(表在 lib/thread.c, 查询在本 TU)");
    if (found == 0) {
        CHECK_EQ(size0, THREAD_STACK_SIZE, "记录里的栈大小 == THREAD_STACK_SIZE");
        CHECK_EQ((usize)base0 % STACK_PAGE, 0, "记录里的栈基址页对齐");
        CHECK_NE(base0, 0, "记录里的栈基址非空");
    } else {
        FAIL("拿不到栈记录, 后续两项跳过");
    }

    /* ---- 3. join 回收 ---- */
    int all_join_ok = 1;
    int all_code_ok = 1;
    int all_val_ok = 1;
    for (int i = 0; i < NTHREADS; i++) {
        int code = -1;
        if (thread_join(tids[i], &code) < 0) all_join_ok = 0;
        if (code != 100 + i) all_code_ok = 0;
        if (thread_vals[i] != i * 10 + 1) all_val_ok = 0;
    }
    CHECK(all_join_ok, "thread_join 全部成功");
    CHECK(all_code_ok, "线程退出码正确传递(100+id)");
    CHECK(all_val_ok, "线程参数正确,各线程写入独立槽位");

    CHECK(record_find(tids[0], 0, 0) < 0, "join 后该 tid 的记录已摘除");
    CHECK_EQ(record_count(), base_records, "全部 join 后表回到基线(不会只增不减)");

    /* ---- 4. 错误路径:必须失败,不能挂住 ---- */
    int code = -99;
    CHECK(thread_join(tids[0], &code) < 0, "重复 join 同一 tid 返回负值");
    CHECK_EQ(code, -99, "失败 join 不写 exit_code");
    CHECK(thread_join(99999, &code) < 0, "join 不存在的 tid 返回负值");
    CHECK(thread_join(gettid(), &code) < 0, "join 自己返回负值(不能自锁)");

    /* ---- 5. 退出码边界:0 / 负数 / 128 / INT_MAX ---- */
    {
        static const int codes[] = {0, -7, 128, 0x7fffffff};
        int n = (int)(sizeof(codes) / sizeof(codes[0]));
        int code_ok = 1;

        for (int i = 0; i < n; i++) {
            int t = thread_create(worker_code, (void *)(isize)codes[i]);
            if (t < 0) {
                code_ok = 0;
                continue;
            }

            int got = 0xdead;
            if (thread_join(t, &got) < 0) {
                code_ok = 0;
                continue;
            }
            if (got != codes[i]) code_ok = 0;
        }

        CHECK(code_ok, "退出码 0/-7/128/INT_MAX 原样返回(128 不被当信号死亡)");
    }

    /* ---- 6. gettid 与 create 返回值一致 ---- */
    {
        g_tid_seen = -1;
        int t = thread_create(worker_tid, 0);

        if (t < 0) {
            FAIL("创建 tid 探针线程失败");
        } else {
            int joined = -1;
            int jr = thread_join(t, &joined);
            CHECK(jr >= 0 && g_tid_seen == t,
                  "线程内 gettid() == thread_create 返回值");
        }
    }

    /* ---- 7. 16 KiB 局部数组:64 KiB 线程栈够用 ---- */
    {
        g_big_sum = -1;
        int t = thread_create(worker_bigstack, 0);

        if (t < 0) {
            FAIL("创建大栈帧线程失败");
        } else {
            int joined = -1;
            thread_join(t, &joined);
            CHECK_EQ(g_big_sum, 260096, "16 KiB 局部数组在 64 KiB 线程栈上正常");
        }
    }

    /* ---- 8. 线程内 malloc/free ---- */
    {
        g_alloc_ok = -1;
        int t = thread_create(worker_alloc, 0);

        if (t < 0) {
            FAIL("创建 malloc 线程失败");
        } else {
            int joined = -1;
            thread_join(t, &joined);
            CHECK_EQ(g_alloc_ok, 1, "线程内 malloc/free 正常(不改主线程堆状态)");
        }
    }

    /* ---- 9. 表满路径 ----
     * 真造 513 个存活线程不现实(每个内核栈要求连续 128 个物理页),
     * 所以直接占满槽位测 add 的返回值和"不覆盖已有记录"这两条契约。
     * 此刻没有其他线程存活, 用完原样还原, 安全。
     */
    {
        static struct thread_stack_record saved[THREAD_MAX_RECORDS];

        for (int i = 0; i < THREAD_MAX_RECORDS; i++) {
            saved[i] = thread_stack_records[i];
        }

        int before = record_count();

        for (int i = 0; i < THREAD_MAX_RECORDS; i++) {
            if (!thread_stack_records[i].used) {
                thread_stack_records[i].used = 1;
                thread_stack_records[i].tid = -2;   /* 不存在的 tid, 不会被 join 摸到 */
                thread_stack_records[i].stack_base = 0;
                thread_stack_records[i].stack_size = 0;
            }
        }

        CHECK_EQ(record_count(), THREAD_MAX_RECORDS, "手工把表填满");

        int probe = 0;
        int r = thread_stack_record_add(-3, &probe, THREAD_STACK_SIZE);
        CHECK_EQ(r, -1, "表满时 thread_stack_record_add 返回 -1");
        CHECK_EQ(record_count(), THREAD_MAX_RECORDS, "表满时不会覆盖已有记录");

        for (int i = 0; i < THREAD_MAX_RECORDS; i++) {
            thread_stack_records[i] = saved[i];
        }
        CHECK_EQ(record_count(), before, "还原后记录数与原来一致");
    }

    TEST_END();
}
