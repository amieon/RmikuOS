/* 110_errno_tls_test.c —— errno 宏化后的可修改左值 + 每线程隔离
 *
 * errno.h 现在是 #define errno (*rk_errno_slot())：
 *   int 本体在 __thread_entry 栈上(lib/thread.c)，主线程走 fallback 全局。
 * 验三件事：
 *   1. errno = x / &errno / 保存-清零-恢复 都还能编译(必须是 lvalue)
 *   2. rk_sys_ret(-ENOENT) —— 用户态唯一的 errno 写入口 —— 写进来能读回
 *   3. 4 个 worker 交错写 errno，各自读回自己的，主线程不受干扰
 *
 * worker 必须自然返回；thread_exit() 会绕过 __thread_entry 的绑定钩子。
 */
#include "user.h"

#define N_WORKER 4
#define N_ROUND  8

static volatile int g_ok[N_WORKER];
static volatile int g_last[N_WORKER];
static int g_failures;

/* 项目 puts 自带换行(stdio.h:628-633)，拼一行得用 fputs。 */
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

static void worker(void *arg) {
    int id = (int)(usize)arg;
    g_ok[id] = 1;

    /* 用 100+ 段当标记值，避免和真 errno 编号混淆；errno 就是个 int。 */
    for (int r = 0; r < N_ROUND; r++) {
        int want = 100 + id * 16 + r;
        errno = want;
        yield();                      /* 故意让别的线程插进来写 errno */
        if (errno != want) g_ok[id] = 0;
    }

    /* 真链路：喂一个模拟的内核 -errno 给 rk_sys_ret，由它写 errno。 */
    int code = EINVAL + id;           /* 22..25，每个线程不同 */
    if (rk_sys_ret(-code) != -1 || errno != code) g_ok[id] = 0;
    g_last[id] = errno;
    /* 自然返回 -> __thread_entry 的 destructor 钩子 */
}

int main(void) {
    puts("errno_tls_test start");

    /* --- 1. 可修改左值 --- */
    errno = EINVAL;
    check(errno == EINVAL, "errno = EINVAL 能赋值并读回");

    int *slot = &errno;
    *slot = ENOENT;
    check(errno == ENOENT, "&errno 取地址后写入生效");

    int saved = errno;
    errno = 0;
    check(errno == 0, "errno = 0 清零(strtol 前的标准写法)");
    errno = saved;
    check(errno == ENOENT, "保存-恢复惯用法(openssl opt.c 同款)");

    /* --- 2. syscall 翻译层 --- */
    check(rk_sys_ret(-ENOENT) == -1 && errno == ENOENT,
          "rk_sys_ret(-ENOENT) 写 errno 并返 -1");
    check(rk_sys_ret(5) == 5 && errno == ENOENT,
          "成功返回值不动 errno");

    /* --- 3. 每线程隔离 --- */
    errno = 777;
    int tids[N_WORKER];
    for (int i = 0; i < N_WORKER; i++) {
        tids[i] = thread_create(worker, (void *)(usize)i);
        if (tids[i] < 0) {
            puts("FAIL: thread_create");
            return 1;
        }
    }
    check(errno == 777, "主线程 errno 未被子线程改动");
    for (int i = 0; i < N_WORKER; i++) thread_join(tids[i], 0);

    int all_ok = 1;
    for (int i = 0; i < N_WORKER; i++) if (!g_ok[i]) all_ok = 0;
    check(all_ok, "4 worker 各写各的 errno，交错后都读回自己的");

    int distinct = 1;
    for (int i = 0; i < N_WORKER; i++)
        if (g_last[i] != EINVAL + i) distinct = 0;
    check(distinct, "rk_sys_ret 在每线程写入各自 errno，互不覆盖");

    check(errno == 777, "join 之后主线程 errno 仍是 777");

    fputs("  main errno = ", stdout);
    printf("%d", errno);
    fputc('\n', stdout);
    for (int i = 0; i < N_WORKER; i++) {
        fputs("  worker ", stdout);
        printf("%d", i);
        fputs(" errno = ", stdout);
        printf("%d", g_last[i]);
        fputc('\n', stdout);
    }

    if (g_failures) {
        fputs("errno_tls_test FAIL, failed checks = ", stdout);
        printf("%d", g_failures);
        fputc('\n', stdout);
        return 1;
    }
    puts("errno_tls_test PASS");
    return 0;
}
