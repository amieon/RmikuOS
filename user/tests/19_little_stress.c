#include "user.h"

#define THREADS 4
#define ROUNDS 40
#define PAGES_PER_THREAD 3
#define PAGE_SIZE 4096

#ifndef PROT_READ
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#endif


static int global_zero;
static char global_bss[8192];
static int thread_done[THREADS];
static int thread_errors[THREADS];
static int thread_exit_codes[THREADS];


static int global_init = 20260606;
static char data_magic[] = "ELF-DATA-OK";

struct thread_arg {
    int id;
    int rounds;
    int pages;
};

static struct thread_arg args[THREADS];



static void print_line3(const char *tag, int a, int b, int c) {
    char buf[128];
    int pos = 0;

    pos = append_str(buf, pos, tag);
    pos = append_str(buf, pos, " ");
    pos = append_int(buf, pos, a);
    pos = append_str(buf, pos, " ");
    pos = append_int(buf, pos, b);
    pos = append_str(buf, pos, " ");
    pos = append_int(buf, pos, c);
    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static int check_elf_globals(void) {
    if (global_zero != 0) {
        puts("FAIL: global_zero is not zero\n");
        return -1;
    }

    for (int i = 0; i < (int)sizeof(global_bss); i++) {
        if (global_bss[i] != 0) {
            puts("FAIL: global_bss is not zero\n");
            return -1;
        }
    }

    if (global_init != 20260606) {
        puts("FAIL: global_init wrong\n");
        return -1;
    }

    if (data_magic[0] != 'E' ||
        data_magic[1] != 'L' ||
        data_magic[2] != 'F' ||
        data_magic[3] != '-' ||
        data_magic[4] != 'D') {
        puts("FAIL: data_magic wrong\n");
        return -1;
    }

    global_bss[0] = 'O';
    global_bss[1] = 'K';
    global_bss[2] = 0;

    if (global_bss[0] != 'O' || global_bss[1] != 'K') {
        puts("FAIL: global_bss not writable\n");
        return -1;
    }

    return 0;
}

static unsigned char pattern(int tid, int round, int page, int offset) {
    return (unsigned char)((tid * 31 + round * 17 + page * 7 + offset) & 0xff);
}

static void worker(void *raw) {
    struct thread_arg *arg = (struct thread_arg *)raw;
    int id = arg->id;
    int rounds = arg->rounds;
    int pages = arg->pages;

    for (int r = 0; r < rounds; r++) {
        char *regions[PAGES_PER_THREAD];

        for (int p = 0; p < pages; p++) {
            regions[p] = (char *)mmap(PAGE_SIZE, PROT_READ | PROT_WRITE);

            if ((isize)regions[p] < 0) {
                thread_errors[id]++;
                print_line3("[worker mmap fail]", id, r, p);
                thread_exit(10 + id);
            }

            for (int j = 0; j < PAGE_SIZE; j++) {
                regions[p][j] = (char)pattern(id, r, p, j);
            }
        }

        yield();

        for (int p = 0; p < pages; p++) {
            for (int j = 0; j < PAGE_SIZE; j++) {
                char expected = (char)pattern(id, r, p, j);

                if (regions[p][j] != expected) {
                    thread_errors[id]++;
                    print_line3("[worker pattern fail]", id, r, p);
                    thread_exit(20 + id);
                }
            }
        }

        for (int p = 0; p < pages; p++) {
            if (munmap(regions[p], PAGE_SIZE) < 0) {
                thread_errors[id]++;
                print_line3("[worker munmap fail]", id, r, p);
                thread_exit(30 + id);
            }
        }

        if (r % 10 == 0) {
            print_line3("[worker progress]", id, r, 0);
        }
    }

    thread_done[id] = 1;
    thread_exit(100 + id);
}

int main(int argc, char *argv[]) {
    puts("elf_mmap_thread_stress start\n");

    if (check_elf_globals() < 0) {
        return 1;
    }

    puts("ELF .data/.bss check OK\n");

    int tids[THREADS];

    for (int i = 0; i < THREADS; i++) {
        args[i].id = i;
        args[i].rounds = ROUNDS;
        args[i].pages = PAGES_PER_THREAD;

        tids[i] = thread_create(worker, &args[i]);

        if (tids[i] < 0) {
            puts("FAIL: thread_create failed\n");
            put_int(i);
            puts("\n");
            return 1;
        }

        print_line3("[main created]", i, tids[i], 0);
    }

    for (int r = 0; r < ROUNDS; r++) {
        char *p = (char *)mmap(PAGE_SIZE, PROT_READ | PROT_WRITE);

        if ((isize)p < 0) {
            puts("FAIL: main mmap failed\n");
            return 1;
        }

        for (int j = 0; j < PAGE_SIZE; j++) {
            p[j] = (char)((r + j) & 0xff);
        }

        yield();

        for (int j = 0; j < PAGE_SIZE; j++) {
            char expected = (char)((r + j) & 0xff);

            if (p[j] != expected) {
                puts("FAIL: main pattern mismatch\n");
                return 1;
            }
        }

        if (munmap(p, PAGE_SIZE) < 0) {
            puts("FAIL: main munmap failed\n");
            return 1;
        }

        if (r % 10 == 0) {
            print_line3("[main progress]", r, 0, 0);
        }
    }

    for (int i = 0; i < THREADS; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        thread_exit_codes[i] = code;

        if (ret != tids[i]) {
            puts("FAIL: thread_join returned wrong tid\n");
            put_int(ret);
            puts(" expected ");
            put_int(tids[i]);
            puts("\n");
            return 1;
        }

        if (code != 100 + i) {
            puts("FAIL: thread exit code wrong\n");
            put_int(code);
            puts(" expected ");
            put_int(100 + i);
            puts("\n");
            return 1;
        }

        if (!thread_done[i]) {
            puts("FAIL: thread_done not set\n");
            put_int(i);
            puts("\n");
            return 1;
        }

        if (thread_errors[i] != 0) {
            puts("FAIL: thread_errors nonzero\n");
            put_int(i);
            puts("\n");
            return 1;
        }

        print_line3("[main joined]", i, ret, code);
    }

    puts("elf_mmap_thread_stress PASS\n");
    return 0;
}