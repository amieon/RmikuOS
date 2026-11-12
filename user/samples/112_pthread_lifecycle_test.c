/* 112_pthread_lifecycle_test.c —— pthread_create / exit / join + cleanup + attr 回归
 *
 * 这一版把六个生命周期函数补齐了。最关键的一条断言是 PHASE 1：
 * 让 start_routine 返回一个高 32 位非零的 void*（仅当位模式用，不解引用），
 * join 回来必须逐位相等。
 *   - 内核若还在 sys_thread_exit 里做 `args[0] as i32`，高 32 位当场丢；
 *   - join 若还用 write_user_i32，只填低 4 字节，高位留栈上垃圾。
 * 两种错都能编译、能链接、跑起来也不崩，只是返回值悄悄变成野指针 ——
 * 正是那种「编译过了，语义错」的形状。
 *
 * 覆盖：
 *   1. 全宽 void* retval 往返 / 多 worker 各自的值不串
 *   2. pthread_exit 从 worker 内部直接退出，retval 仍可 join
 *   3. cleanup handler：push/pop(0)/pop(1)、未 pop 时 LIFO、与 TSD dtor 的先后
 *   4. attr_init 清零、detachstate 自动分离且 join 被拒、stacksize 真生效
 *   5. 错误码：自 join、空参数、非法 attr 值
 *
 * 所有 phase 串行执行（join 完再进下一 phase），这样全局记录数组不用加锁。
 */
#include "user.h"
#include "pthread.h"
#include "thread.h"

#define N_WORKER    4
#define YIELD_WAIT  4000
#define BIG_STACK   (128 * 1024)

static int g_failures;
static volatile int g_done;
static volatile int g_note_n;
static int g_notes[8];
static volatile pthread_t g_self_tid;

/* ---- 只造位模式、绝不解引用的返回值 ---- */
static int g_anchor;

static void *make_retval(unsigned tag) {
    usize v = (usize)&g_anchor;
    if (sizeof(usize) >= 8) {
        /* 运行期变量移位：写死 <<32 在宿主 LLP64(4 字节 usize) 下会报
         * shift count >= width 的 warning。真机 shift=32，高 32 位必非零。 */
        unsigned shift = (unsigned)(sizeof(usize) * 4);
        v |= ((usize)0xabcd) << shift;
    }
    return (void *)(v | (usize)tag);
}

/* 项目 puts 自带换行，拼一行只能用 fputs。 */
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

static void wait_and_settle(void) {
    for (int i = 0; i < YIELD_WAIT && !g_done; i++) RK_PTHREAD_YIELD();
    yield_rounds(YIELD_WAIT / 2);
}

static void note(int v) {
    if (g_note_n < 8) g_notes[g_note_n++] = v;
}

/* ---- PHASE 1/2/5：普通 start_routine ---- */
static void *return_tagged(void *arg) {
    unsigned tag = (unsigned)(usize)arg;
    for (volatile int i = 0; i < 2000; i++) { }   /* 制造多 worker 交错 */
    return make_retval(tag);
}

/* self 单独一个 worker：tid 只能由 pthread_create 返回，若在 create 之后
 * 才把期望值写进全局，worker 可能早就读到了 0 —— 那是测试自己的竞态，
 * 不是实现的错。这里反过来让 worker 上报，join 完再比。 */
static void *return_tagged_self(void *arg) {
    g_self_tid = pthread_self();
    return make_retval((unsigned)(usize)arg);
}

static void *plain_worker(void *arg) {
    (void)arg;
    g_done = 1;
    return make_retval(55);
}

/* ---- cleanup handler 与 TSD destructor 共用一条顺序记录 ---- */
static void cleanup_note(void *arg) {
    note((int)(usize)arg);
}

static pthread_key_t g_dtor_key;
static void dtor_note(void *arg) {
    (void)arg;
    note(90);
}

/* POSIX 要求 push/pop 在同一词法作用域配对（两者都是宏），所以 pop 必须在
 * 源码里出现；但 pthread_exit 在块内就陷入、永不返回，两个 handler 被退出
 * 时仍未 pop —— 这时才该由 pthread_exit 按 LIFO 跑 2、1，再跑 TSD dtor 90。
 * 下面两个 pop(0) 纯粹用于闭合宏，跟在 noreturn 之后不可达。 */
static void *exit_with_cleanup(void *arg) {
    (void)arg;
    pthread_setspecific(g_dtor_key, (void *)1);
    pthread_cleanup_push(cleanup_note, (void *)(usize)1);
    pthread_cleanup_push(cleanup_note, (void *)(usize)2);
    pthread_exit(make_retval(7));
    pthread_cleanup_pop(0);
    pthread_cleanup_pop(0);
    return 0;
}

/* pop(0) 不执行、pop(1) 立即执行，正常 return 时不再跑链上残留。 */
static void *pop_worker(void *arg) {
    (void)arg;
    pthread_cleanup_push(cleanup_note, (void *)(usize)10);
    pthread_cleanup_pop(0);
    pthread_cleanup_push(cleanup_note, (void *)(usize)11);
    pthread_cleanup_pop(1);
    return make_retval(9);
}

int main(void) {
    fputs("== pthread lifecycle regression ==\n", stdout);

    /* ---- PHASE 1: 全宽 retval 往返 ---- */
    g_done = 0;
    void *out = 0;
    pthread_t t1;
    int rc = pthread_create(&t1, 0, return_tagged_self, (void *)(usize)3);
    check(rc == 0, "phase1 pthread_create returns 0");
    rc = pthread_join(t1, &out);
    check(rc == 0, "phase1 pthread_join returns 0");
    check(g_self_tid == t1, "phase1 pthread_self() equals pthread_t from create");
    check(pthread_equal(t1, t1) && !pthread_equal(t1, (pthread_t)999),
          "phase1 pthread_equal");
    check(out == make_retval(3), "phase1 void* retval survived full-width round trip");
    if (out != make_retval(3)) {
        fputs("  info : got ", stdout);
        put_hex((usize)out);
        fputs(" expected ", stdout);
        put_hex((usize)make_retval(3));
        fputc('\n', stdout);
    }

    /* ---- PHASE 2: 多 worker，各自的 retval 不串 ---- */
    pthread_t ts[N_WORKER];
    void *outs[N_WORKER];
    for (int i = 0; i < N_WORKER; i++) {
        outs[i] = 0;
        rc = pthread_create(&ts[i], 0, return_tagged, (void *)(usize)(20 + i));
        check(rc == 0, "phase2 pthread_create");
    }
    for (int i = 0; i < N_WORKER; i++) {
        check(pthread_join(ts[i], &outs[i]) == 0, "phase2 pthread_join");
    }
    int all_match = 1;
    for (int i = 0; i < N_WORKER; i++) {
        if (outs[i] != make_retval(20 + i)) all_match = 0;
    }
    check(all_match, "phase2 each worker's retval stayed isolated");

    /* ---- PHASE 3: pthread_exit + cleanup handler + TSD dtor ---- */
    check(pthread_key_create(&g_dtor_key, dtor_note) == 0,
          "phase3 pthread_key_create with destructor");
    g_note_n = 0;
    out = 0;
    pthread_t t3;
    check(pthread_create(&t3, 0, exit_with_cleanup, 0) == 0,
          "phase3 pthread_create");
    check(pthread_join(t3, &out) == 0, "phase3 join thread that called pthread_exit");
    check(out == make_retval(7), "phase3 pthread_exit retval reached join");
    check(g_note_n == 3, "phase3 two cleanup handlers + one TSD dtor ran");
    check(g_notes[0] == 2 && g_notes[1] == 1,
          "phase3 unpopped cleanup handlers ran LIFO");
    check(g_notes[2] == 90, "phase3 cleanup handlers precede TSD destructor");

    /* ---- PHASE 4: pop(0) / pop(1) 与正常 return ---- */
    g_note_n = 0;
    out = 0;
    pthread_t t4;
    check(pthread_create(&t4, 0, pop_worker, 0) == 0, "phase4 pthread_create");
    check(pthread_join(t4, &out) == 0, "phase4 pthread_join");
    check(out == make_retval(9), "phase4 normal return carries retval");
    check(g_note_n == 1 && g_notes[0] == 11,
          "phase4 pop(0) skipped / pop(1) executed, no extra handlers");
    pthread_key_delete(g_dtor_key);

    /* ---- PHASE 5: attr_init 清零 + detachstate + stacksize ---- */
    pthread_attr_t a;
    for (unsigned i = 0; i < sizeof(a.opaque) / sizeof(a.opaque[0]); i++) {
        a.opaque[i] = 0xdeadbeefUL;
    }
    check(pthread_attr_init(&a) == 0, "phase5 pthread_attr_init");
    int detachstate = -1;
    usize stacksize = 0;
    check(pthread_attr_getdetachstate(&a, &detachstate) == 0 &&
          detachstate == PTHREAD_CREATE_JOINABLE,
          "phase5 attr_init clears opaque (no random detached threads)");
    check(pthread_attr_getstacksize(&a, &stacksize) == 0 &&
          stacksize == PTHREAD_STACK_DEFAULT,
          "phase5 default stack size");
    check(pthread_attr_setdetachstate(&a, 999) == EINVAL,
          "phase5 invalid detachstate rejected");
    check(pthread_attr_setstacksize(&a, 1024) == EINVAL,
          "phase5 stack below PTHREAD_STACK_MIN rejected");

    /* stacksize 真往下传：查 thread_stack_records 里登记的长度。 */
    pthread_attr_t a_stack;
    pthread_attr_init(&a_stack);
    pthread_attr_setstacksize(&a_stack, BIG_STACK);
    g_done = 0;
    pthread_t t5;
    check(pthread_create(&t5, &a_stack, plain_worker, 0) == 0,
          "phase5 pthread_create with 128 KiB stack");
    void *base = 0;
    usize size = 0;
    check(thread_stack_record_peek((int)(usize)t5, &base, &size) == 0 &&
          size == BIG_STACK,
          "phase5 attr stacksize really reached the thread stack mapping");
    check(pthread_join(t5, 0) == 0, "phase5 pthread_join with NULL retval");
    check(g_done == 1, "phase5 worker ran");

    /* detachstate：create 内部自动 detach，join 必须被内核拒。 */
    pthread_attr_t a_det;
    pthread_attr_init(&a_det);
    pthread_attr_setdetachstate(&a_det, PTHREAD_CREATE_DETACHED);
    g_done = 0;
    pthread_t t6;
    check(pthread_create(&t6, &a_det, plain_worker, 0) == 0,
          "phase5 pthread_create with PTHREAD_CREATE_DETACHED");
    wait_and_settle();
    check(g_done == 1, "phase5 detached worker ran to completion");
    /* 两种错误码都合法：worker 还在 Zombie 时内核返 EINVAL；若 back-from
     * 已经把 TCB reap 掉，try_thread 找不到 tid 就返 ESRCH。POSIX 把
     * join(detached) 定为未定义行为，这里只要求「一定失败」。 */
    check(pthread_join(t6, 0) != 0,
          "phase5 join on detached thread fails (EINVAL or ESRCH)");

    /* ---- PHASE 6: 错误码 ---- */
    check(pthread_join(pthread_self(), 0) == EDEADLK,
          "phase6 self-join returns EDEADLK");
    check(pthread_create(0, 0, plain_worker, 0) == EINVAL,
          "phase6 NULL pthread_t rejected");
    pthread_t t7;
    check(pthread_create(&t7, 0, 0, 0) == EINVAL,
          "phase6 NULL start_routine rejected");
    check(pthread_join((pthread_t)999999, 0) == ESRCH,
          "phase6 unknown tid returns ESRCH");

    if (g_failures) {
        fputs("pthread_lifecycle_test FAIL\n", stdout);
        return 1;
    }
    fputs("pthread_lifecycle_test PASS\n", stdout);
    return 0;
}
