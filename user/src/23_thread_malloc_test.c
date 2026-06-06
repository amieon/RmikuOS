#include "user.h"
#include "lock.h"

#define THREADS 6
#define ITERS 2000

static int counter;
static int errors;
static mutex_t lock = MUTEX_INIT;

struct arg {
    int id;
};

static struct arg args[THREADS];

static void worker(void *raw) {
    struct arg *a = (struct arg *)raw;

    for (int i = 0; i < ITERS; i++) {
        mutex_lock(&lock);

        int old = counter;

        /*
         * 故意 yield，扩大竞争窗口。
         * 如果锁坏了，counter 很容易错。
         */
        if ((i % 16) == 0) {
            yield();
        }

        counter = old + 1;

        mutex_unlock(&lock);

        if ((i % 123) == 0) {
            yield();
        }
    }

    thread_exit(100 + a->id);
}

int main() {
    puts("lock_test start\n");

    counter = 0;
    errors = 0;
    mutex_init(&lock);

    int tids[THREADS];

    for (int i = 0; i < THREADS; i++) {
        args[i].id = i;

        tids[i] = thread_create(worker, &args[i]);

        if (tids[i] < 0) {
            puts("FAIL: thread_create\n");
            return 1;
        }
    }

    for (int i = 0; i < THREADS; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        if (ret != tids[i] || code != 100 + i) {
            puts("FAIL: join\n");
            return 1;
        }
    }

    int expected = THREADS * ITERS;

    puts("counter=");
    put_int(counter);
    puts(" expected=");
    put_int(expected);
    puts("\n");

    if (counter != expected) {
        puts("FAIL: counter mismatch\n");
        return 1;
    }

    puts("lock_test PASS\n");
    return 0;
}

