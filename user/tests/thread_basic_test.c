#include "test.h"

/* 用户态线程基础:create / join / 退出码 / 参数传递 */
#define NTHREADS 4
static int thread_vals[NTHREADS];

static void worker(void *arg) {
    int id = (int)(isize)arg;
    thread_vals[id] = id * 10 + 1;   /* 每个线程写自己的槽位 */
    thread_exit(id + 100);           /* RmikuOS 的 thread_exit 直接收 int 退出码 */
}

int main() {
    TEST_START("thread_basic");

    int tids[NTHREADS];
    int ok = 1;
    for (int i = 0; i < NTHREADS; i++) {
        thread_vals[i] = -1;
        tids[i] = thread_create(worker, (void *)(isize)i);
        if (tids[i] < 0) ok = 0;
    }
    CHECK(ok, "thread_create 全部成功");

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

    TEST_END();
}
s