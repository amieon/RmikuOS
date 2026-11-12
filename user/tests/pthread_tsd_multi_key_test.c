/* pthread_tsd_multi_key_test.c —— 一个线程多个 key / 多个线程各多个 key
 *
 * key 本来就是内核 TCB::tsd[] 的下标，不是「映射到下标」：
 *   SYS_TSD_GET(k) -> sys_tsd_get(k) -> tsd_get_current(k) -> TCB::tsd[k]
 * pthread_key_create() 只在「分配时」发一个没被占用的号，并把 destructor
 * 登记到那一列；寻址时没有任何第二跳。
 *
 * PHASE A：单个线程（主线程）同时用 3 个 key，各存各的互不覆盖。
 * PHASE B：4 个 worker × 3 个 key = 12 个格子，每个格子只被自己的线程读到。
 */
#include "test.h"
#include "pthread.h"

#define N_WORKER   4
#define N_KEY      3
#define N_ROUND    8
#define GRID_BASE  0x1000

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_key_t  g_key[N_KEY];       /* 3 个出参，由 init_keys 写回 */
static volatile int   g_init_hits;
static volatile int   g_create_failed;

static int g_grid[N_WORKER][N_KEY];       /* (worker, key) 一格一个值 */
static int g_seen[N_WORKER][N_KEY];       /* 无参取回且是自己的次数 */
static int g_errors[N_WORKER][N_KEY];
static int g_cleaned[N_WORKER][N_KEY];    /* destructor 次数 */
static int g_main_vals[N_KEY];            /* PHASE A 主线程自己的 3 个值 */

static int expected(int id, int k) { return GRID_BASE + id * 16 + k; }

/* 值自带坐标，不必做跨子数组的指针算术。主线程 PHASE A 的值不在
 * GRID_BASE 范围里，会被边界检查挡住，不会被误当成 worker 格子。 */
static void cell_destructor(void *value) {
    int *cell = (int *)value;
    if (!cell) return;
    int off = *cell - GRID_BASE;
    int id = off / 16, k = off % 16;
    if (id < 0 || id >= N_WORKER || k < 0 || k >= N_KEY) return;
    g_cleaned[id][k]++;
}

/* 一次领 3 个号：前两个登记 destructor，第三个传 NULL（不清理）。
 * 这正是 destructor 挂在「列」上的意思：同一份逻辑，是否启用按 key 定。 */
static void init_keys(void) {
    g_init_hits++;
    for (int k = 0; k < N_KEY; k++) {
        void (*dtor)(void *) = (k == 2) ? 0 : cell_destructor;
        if (pthread_key_create(&g_key[k], dtor) != 0) g_create_failed++;
    }
}

/* ---------- PHASE A：一个线程，多个 key ----------
 * 单看这一段：static void *slot[3] 直接存指针完全等价，TSD 是杀鸡用牛刀。
 * 这里仍走 set/get 是为了当对照组：验「同线程内 3 个 key 不互相覆盖」，
 * 并给主线程留 3 格，供 main() 末尾检查「主线程的值没被子线程干扰」。
 * 分叉点在 PHASE B：slot[3] 全进程只有一份，4 个 worker 会互相覆盖；
 * 而 TSD 的 tsd[k] 在每个 TCB 里各有一行，worker 用同一个 g_key[k] 读各的。 */
static int phase_a(void) {
    g_main_vals[0] = 11;
    g_main_vals[1] = 22;
    g_main_vals[2] = 33;

    int ok = 1;
    for (int k = 0; k < N_KEY; k++) {
        if (pthread_setspecific(g_key[k], &g_main_vals[k]) != 0) ok = 0;
    }
    for (int k = 0; k < N_KEY; k++) {
        int *got = (int *)pthread_getspecific(g_key[k]);
        if (got != &g_main_vals[k] || *got != g_main_vals[k]) ok = 0;
    }
    return ok;
}

/* ---------- PHASE B：多个 worker，每个都用多个 key ---------- */
static void worker(void *arg) {
    int id = (int)(usize)arg;

    pthread_once(&g_once, init_keys);   /* 主线程已领过号，这里不该再跑 init */

    for (int k = 0; k < N_KEY; k++) {
        g_grid[id][k]   = expected(id, k);
        g_seen[id][k]   = 0;
        g_errors[id][k] = 0;
        g_cleaned[id][k] = 0;
        if (pthread_setspecific(g_key[k], &g_grid[id][k]) != 0) g_errors[id][k]++;
    }

    for (int r = 0; r < N_ROUND; r++) {
        yield();                        /* 让别的 worker 插进来写自己的格子 */
        for (int k = 0; k < N_KEY; k++) {
            int *got = (int *)pthread_getspecific(g_key[k]);
            if (got == &g_grid[id][k] && *got == expected(id, k)) {
                g_seen[id][k]++;
            } else {
                g_errors[id][k]++;
            }
        }
    }
    /* 自然返回 -> __thread_entry 跑 destructor；调 thread_exit() 会绕过它 */
}

int main(void) {
    TEST_START("pthread_tsd_multi_key");

    /* 主线程先领号，PHASE A 才能用；worker 里再调 once 用来验「不重跑」。 */
    pthread_once(&g_once, init_keys);

    CHECK_EQ(g_init_hits, 1, "pthread_once: init 只跑一次");
    CHECK_EQ(g_create_failed, 0, "pthread_key_create: 3 个号全部领到");
    CHECK(g_key[0] != g_key[1] && g_key[1] != g_key[2] && g_key[0] != g_key[2],
          "3 个 key 是不同的下标");

    /* PHASE A */
    CHECK_EQ(phase_a(), 1, "PHASE A: 同一线程 3 个 key 各存各的，互不覆盖");

    /* PHASE B */
    int tids[N_WORKER];
    int created_ok = 1;
    for (int i = 0; i < N_WORKER; i++) {
        tids[i] = thread_create(worker, (void *)(usize)i);
        if (tids[i] < 0) created_ok = 0;
    }
    CHECK(created_ok, "PHASE B: 4 个 worker 全部创建成功");
    for (int i = 0; i < N_WORKER; i++) {
        if (tids[i] >= 0) thread_join(tids[i], 0);
    }

    int seen = 0, errors = 0, cleaned_yes = 0, cleaned_no = 0, grid_ok = 1;
    for (int id = 0; id < N_WORKER; id++) {
        for (int k = 0; k < N_KEY; k++) {
            seen    += g_seen[id][k];
            errors  += g_errors[id][k];
            if (g_grid[id][k] != expected(id, k)) grid_ok = 0;
        }
        cleaned_yes += g_cleaned[id][0] + g_cleaned[id][1];
        cleaned_no  += g_cleaned[id][2];
    }

    CHECK_EQ(g_init_hits, 1, "worker 并发调 once 没有重复领号");
    CHECK_EQ(grid_ok, 1, "12 个格子的值各不相同");
    CHECK_EQ(seen, N_WORKER * N_KEY * N_ROUND,
             "12 个格子每格 8 次都取回自己那一个");
    CHECK_EQ(errors, 0, "没有任何一次串到别的 worker / 别的 key");
    CHECK_EQ(cleaned_yes, N_WORKER * 2,
             "登记了 destructor 的 2 个 key，各线程各清理一次");
    CHECK_EQ(cleaned_no, 0, "第 3 个 key 没登记 destructor，不会被清理");

    int main_intact = 1;
    for (int k = 0; k < N_KEY; k++) {
        if (pthread_getspecific(g_key[k]) != &g_main_vals[k]) main_intact = 0;
    }
    CHECK_EQ(main_intact, 1, "主线程自己的 3 格没被子线程干扰");

    for (int k = 0; k < N_KEY; k++) pthread_setspecific(g_key[k], 0);
    int del_ok = 1;
    for (int k = 0; k < N_KEY; k++) {
        if (pthread_key_delete(g_key[k]) != 0) del_ok = 0;
    }
    CHECK_EQ(del_ok, 1, "3 个 key 逐个注销");
    CHECK_EQ(pthread_setspecific(g_key[0], (void *)1), EINVAL,
             "注销后 setspecific 返回 EINVAL");
    CHECK_EQ(pthread_key_delete(g_key[0]), EINVAL, "重复注销返回 EINVAL");

    TEST_END();
}
