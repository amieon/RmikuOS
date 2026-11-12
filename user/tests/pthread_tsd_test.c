/* pthread_tsd_test.c —— 线程私有数据(TSD)全链路回归
 *
 * 用到的 5 个函数：
 *   pthread_once()          保证 key 只领一次
 *   pthread_key_create()    领编号 + 登记 destructor
 *   pthread_setspecific()   把值放进「当前线程」那一格
 *   pthread_getspecific()   无参取回当前线程的值
 *   pthread_key_delete()    注销编号
 * destructor 不是第 6 个函数，是 key_create 的第二个参数：线程退出时自动跑。
 *
 * worker 必须「自然返回」，不要调 thread_exit(code)：后者直接发
 * SYS_THREAD_EXIT，会绕过 __thread_entry 里的 rk_tsd_run_destructors()。
 */
#include "test.h"
#include "pthread.h"

#define N_WORKER   4
#define N_LOOKUP   8
#define CTX_MAGIC  0x5a5a

struct worker_ctx {
    volatile int id;
    volatile int magic;
    volatile int seen;            /* 无参取回且确认是自己的次数 */
    volatile int errors;
    volatile int cleaned;         /* destructor 跑过 */
    volatile int cleared_before;  /* dtor 里看到槽已被清成 NULL */
};

static struct worker_ctx g_ctx[N_WORKER];
static pthread_once_t    g_once = PTHREAD_ONCE_INIT;
/* 出参：由 init_key() 里的 key_create 写回。零初始化得到的 0 不是有效 key。 */
static pthread_key_t     g_key;
static volatile int      g_init_hits;
static volatile int      g_create_failed;

/* key_create 时登记的清理纸条：线程退出时由 __thread_entry 自动调用。 */
static void ctx_destructor(void *value) {
    struct worker_ctx *c = (struct worker_ctx *)value;
    if (!c || c->magic != CTX_MAGIC) return;
    /* POSIX 规定先清 NULL 再调 dtor，这里正好验一次。 */
    c->cleared_before = (pthread_getspecific(g_key) == 0);
    c->cleaned = 1;
}

/* once 的 init：整个进程只跑一次，负责统一领号。 */
static void init_key(void) {
    g_init_hits++;
    if (pthread_key_create(&g_key, ctx_destructor) != 0) g_create_failed = 1;
}

/* 模拟第三方库里的无参取上下文函数：签名里没有 ctx，只能靠 TSD。 */
static struct worker_ctx *current_ctx(void) {
    return (struct worker_ctx *)pthread_getspecific(g_key);
}

static void worker(void *arg) {
    int id = (int)(usize)arg;
    struct worker_ctx *mine = &g_ctx[id];

    pthread_once(&g_once, init_key);   /* 4 个线程并发调，init 只跑一次 */

    mine->id = id;
    mine->magic = CTX_MAGIC;
    mine->seen = 0;
    mine->errors = 0;
    mine->cleaned = 0;
    mine->cleared_before = 0;

    if (pthread_setspecific(g_key, mine) != 0) mine->errors++;

    /* 传 NULL 是清空这一格，不是「存了个空值」 */
    if (pthread_setspecific(g_key, 0) != 0) mine->errors++;
    if (pthread_getspecific(g_key) != 0) mine->errors++;
    if (pthread_setspecific(g_key, mine) != 0) mine->errors++;

    for (int i = 0; i < N_LOOKUP; i++) {
        struct worker_ctx *got = current_ctx();
        if (got == mine && got->magic == CTX_MAGIC && got->id == id) {
            mine->seen++;
        } else {
            mine->errors++;
        }
        yield();                       /* 故意让别的线程插进来 set 自己的值 */
    }
    /* 自然返回 -> __thread_entry 跑 destructor -> sys_thread_exit_raw(0) */
}

int main(void) {
    TEST_START("pthread_tsd");

    int tids[N_WORKER];
    int created_ok = 1;
    for (int i = 0; i < N_WORKER; i++) {
        tids[i] = thread_create(worker, (void *)(usize)i);
        if (tids[i] < 0) created_ok = 0;
    }
    CHECK(created_ok, "4 个 worker 全部创建成功");
    for (int i = 0; i < N_WORKER; i++) {
        if (tids[i] >= 0) thread_join(tids[i], 0);
    }

    int seen = 0, errors = 0, cleaned = 0, cleared = 0;
    for (int i = 0; i < N_WORKER; i++) {
        seen    += g_ctx[i].seen;
        errors  += g_ctx[i].errors;
        cleaned += g_ctx[i].cleaned;
        cleared += g_ctx[i].cleared_before;
    }

    CHECK_EQ(g_init_hits, 1, "pthread_once: init 恰好执行一次");
    CHECK_EQ(g_create_failed, 0, "pthread_key_create: 领到编号并登记 destructor");
    CHECK_EQ(errors, 0, "pthread_setspecific: 存/清空/再存 无错误");
    CHECK_EQ(seen, N_WORKER * N_LOOKUP,
             "pthread_getspecific: 每线程只取回自己的 ctx");
    CHECK_EQ(cleaned, N_WORKER, "destructor: 每个线程退出各自动清理一次");
    CHECK_EQ(cleared, N_WORKER, "destructor: 调用前槽已被清成 NULL");
    CHECK(pthread_getspecific(g_key) == 0, "主线程没 set 过，读到 NULL");
    CHECK_EQ(pthread_key_delete(g_key), 0, "pthread_key_delete: 注销成功");
    CHECK_EQ(pthread_setspecific(g_key, (void *)1), EINVAL,
             "注销后 setspecific 返回 EINVAL");
    CHECK(pthread_getspecific(g_key) == 0, "注销后 getspecific 返回 NULL");
    CHECK_EQ(pthread_key_delete(g_key), EINVAL, "重复注销返回 EINVAL");

    TEST_END();
}
