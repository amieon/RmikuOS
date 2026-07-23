#include "user.h"

#define ROUNDS 100

static int done;

static void worker(void *arg) {
    int id = (int)(usize)arg;

    char local[1024];

    for (int i = 0; i < 1024; i++) {
        local[i] = (char)(id + i);
    }

    for (int i = 0; i < 1024; i++) {
        if (local[i] != (char)(id + i)) {
            thread_exit(2);
        }
    }

    done++;
    thread_exit(100 + id);
}

int main() {
    puts("thread_stack_reuse_test start\n");

    done = 0;

    for (int i = 0; i < ROUNDS; i++) {
        int id = i % 20;

        int tid = thread_create(worker, (void *)(usize)id);

        if (tid < 0) {
            puts("FAIL: thread_create at ");
            put_int(i);
            puts("\n");
            return 1;
        }

        int code = -1;
        int ret = thread_join(tid, &code);

        if (ret != tid) {
            puts("FAIL: bad join tid\n");
            return 1;
        }

        if (code != 100 + id) {
            puts("FAIL: bad exit code ");
            put_int(code);
            puts(" expected ");
            put_int(100 + id);
            puts("\n");
            return 1;
        }

        if (i % 10 == 0) {
            puts("[thread_stack_reuse] round=");
            put_int(i);
            puts("\n");
        }
    }

    if (done != ROUNDS) {
        puts("FAIL: done mismatch\n");
        put_int(done);
        puts("\n");
        return 1;
    }

    puts("thread_stack_reuse_test PASS\n");
    return 0;
}