#include "user.h"

#ifndef PROT_READ
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#endif

#define CHILDREN 3
#define THREADS_PER_CHILD 6
#define ROUNDS 80
#define AREAS_PER_ROUND 4
#define PAGE_SIZE 4096
#define FILE_BUF_SIZE 4096


static int global_zero;
static char global_bss_big[32768];
static int thread_done[THREADS_PER_CHILD];
static int thread_errors[THREADS_PER_CHILD];
static int thread_exit_codes[THREADS_PER_CHILD];


static int global_magic = 0x20260606;
static char data_banner[] = "RmikuOS-ELF-DATA-MEGA-STRESS";

struct thread_arg {
    int child_id;
    int tid_index;
    int rounds;
};

static struct thread_arg args[THREADS_PER_CHILD];


static void log4(const char *tag, int a, int b, int c, int d) {
    char buf[160];
    int pos = 0;

    pos = append_str(buf, pos, tag);
    pos = append_str(buf, pos, " ");
    pos = append_int(buf, pos, a);
    pos = append_str(buf, pos, " ");
    pos = append_int(buf, pos, b);
    pos = append_str(buf, pos, " ");
    pos = append_int(buf, pos, c);
    pos = append_str(buf, pos, " ");
    pos = append_int(buf, pos, d);
    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static unsigned char pattern(int child_id, int tid, int round, int area, int off) {
    unsigned int x = 0x12345678u;

    x ^= (unsigned int)(child_id + 1) * 1103515245u;
    x ^= (unsigned int)(tid + 3) * 2654435761u;
    x ^= (unsigned int)(round + 7) * 97531u;
    x ^= (unsigned int)(area + 11) * 31337u;
    x ^= (unsigned int)off * 17u;

    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;

    return (unsigned char)(x & 0xff);
}

static int check_elf_globals_once(void) {
    if (global_zero != 0) {
        puts("FAIL: global_zero not zero\n");
        return -1;
    }

    for (int i = 0; i < (int)sizeof(global_bss_big); i++) {
        if (global_bss_big[i] != 0) {
            puts("FAIL: global_bss_big not zero\n");
            return -1;
        }
    }

    if (global_magic != 0x20260606) {
        puts("FAIL: global_magic wrong\n");
        return -1;
    }

    if (data_banner[0] != 'R' ||
        data_banner[1] != 'm' ||
        data_banner[2] != 'i' ||
        data_banner[3] != 'k' ||
        data_banner[4] != 'u') {
        puts("FAIL: data_banner wrong\n");
        return -1;
    }

    global_bss_big[0] = 'O';
    global_bss_big[1] = 'K';
    global_bss_big[2] = 0;

    if (global_bss_big[0] != 'O' || global_bss_big[1] != 'K') {
        puts("FAIL: bss not writable\n");
        return -1;
    }


    global_bss_big[0] = 0;
    global_bss_big[1] = 0;
    global_bss_big[2] = 0;

    return 0;
}

static int do_file_read_probe(int child_id, int tid, int round) {
    int fd = open("/etc/motd",O_RDWR);

    if (fd < 0) {
        log4("[file open fail]", child_id, tid, round, fd);
        return -1;
    }

    char *buf = (char *)mmap(FILE_BUF_SIZE, PROT_READ | PROT_WRITE);

    if ((isize)buf < 0) {
        close(fd);
        log4("[file mmap fail]", child_id, tid, round, 0);
        return -1;
    }

    isize n = read(fd, buf, FILE_BUF_SIZE);

    if (n < 0) {
        munmap(buf, FILE_BUF_SIZE);
        close(fd);
        log4("[file read fail]", child_id, tid, round, (int)n);
        return -1;
    }


    if (n > 0) {
        volatile char c = buf[0];
        (void)c;
    }

    if (munmap(buf, FILE_BUF_SIZE) < 0) {
        close(fd);
        log4("[file munmap fail]", child_id, tid, round, 0);
        return -1;
    }

    if (close(fd) < 0) {
        log4("[file close fail]", child_id, tid, round, 0);
        return -1;
    }

    return 0;
}

static void worker(void *raw) {
    struct thread_arg *arg = (struct thread_arg *)raw;

    int child_id = arg->child_id;
    int tid = arg->tid_index;
    int rounds = arg->rounds;

    for (int r = 0; r < rounds; r++) {
        char *areas[AREAS_PER_ROUND];
        usize lens[AREAS_PER_ROUND];

        for (int a = 0; a < AREAS_PER_ROUND; a++) {

            int pages = 1 + ((child_id + tid + r + a) % 4);
            lens[a] = (usize)pages * PAGE_SIZE;

            areas[a] = (char *)mmap(lens[a], PROT_READ | PROT_WRITE);

            if ((isize)areas[a] < 0) {
                thread_errors[tid]++;
                log4("[worker mmap fail]", child_id, tid, r, a);
                thread_exit(10 + tid);
            }

            for (usize off = 0; off < lens[a]; off++) {
                areas[a][off] = (char)pattern(child_id, tid, r, a, (int)off);
            }
        }

        /*
         * 让线程在 mmap 后、校验前被打断，增加调度交错。
         */
        yield();

        for (int a = 0; a < AREAS_PER_ROUND; a++) {

            for (usize off = 0; off < lens[a]; off++) {
                char expected = (char)pattern(child_id, tid, r, a, (int)off);

                if (areas[a][off] != expected) {
                    thread_errors[tid]++;
                    log4("[worker pattern fail]", child_id, tid, r, a);
                    thread_exit(20 + tid);
                }
            }
        }


        if ((r % 13) == (tid % 13)) {
            if (do_file_read_probe(child_id, tid, r) < 0) {
                thread_errors[tid]++;
                thread_exit(40 + tid);
            }
        }

        for (int a = 0; a < AREAS_PER_ROUND; a++) {
            if (munmap(areas[a], lens[a]) < 0) {
                thread_errors[tid]++;
                log4("[worker munmap fail]", child_id, tid, r, a);
                thread_exit(30 + tid);
            }
        }

        if (r % 20 == 0) {
            log4("[worker progress]", child_id, tid, r, getpid());
        }

        yield();
    }

    thread_done[tid] = 1;
    thread_exit(100 + tid);
}

static int child_main(int child_id) {

    if (check_elf_globals_once() < 0) {
        return 1;
    }

    for (int i = 0; i < THREADS_PER_CHILD; i++) {
        thread_done[i] = 0;
        thread_errors[i] = 0;
        thread_exit_codes[i] = -1;

        args[i].child_id = child_id;
        args[i].tid_index = i;
        args[i].rounds = ROUNDS;
    }

    int tids[THREADS_PER_CHILD];

    for (int i = 0; i < THREADS_PER_CHILD; i++) {
        tids[i] = thread_create(worker, &args[i]);

        if (tids[i] < 0) {
            log4("[child thread_create fail]", child_id, i, tids[i], getpid());
            return 2;
        }

        log4("[child created]", child_id, i, tids[i], getpid());
    }


    for (int r = 0; r < ROUNDS; r++) {
        usize len = (usize)(1 + ((child_id + r) % 5)) * PAGE_SIZE;
        char *p = (char *)mmap(len, PROT_READ | PROT_WRITE);

        if ((isize)p < 0) {
            log4("[child main mmap fail]", child_id, r, 0, getpid());
            return 3;
        }

        for (usize off = 0; off < len; off++) {
            p[off] = (char)pattern(child_id, 99, r, 0, (int)off);
        }

        yield();

        for (usize off = 0; off < len; off++) {
            char expected = (char)pattern(child_id, 99, r, 0, (int)off);

            if (p[off] != expected) {
                log4("[child main pattern fail]", child_id, r, (int)off, getpid());
                return 4;
            }
        }

        if (munmap(p, len) < 0) {
            log4("[child main munmap fail]", child_id, r, 0, getpid());
            return 5;
        }

        if (r % 20 == 0) {
            log4("[child main progress]", child_id, r, 0, getpid());
        }
    }

    for (int i = 0; i < THREADS_PER_CHILD; i++) {
        int code = -1;
        int ret = thread_join(tids[i], &code);

        thread_exit_codes[i] = code;

        if (ret != tids[i]) {
            log4("[join bad tid]", child_id, i, ret, tids[i]);
            return 10;
        }

        if (code != 100 + i) {
            log4("[join bad code]", child_id, i, code, 100 + i);
            return 11;
        }

        if (!thread_done[i]) {
            log4("[thread not done]", child_id, i, 0, 0);
            return 12;
        }

        if (thread_errors[i] != 0) {
            log4("[thread errors]", child_id, i, thread_errors[i], 0);
            return 13;
        }

        log4("[child joined]", child_id, i, ret, code);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    puts("mega_elf_mmap_thread_fork_stress start\n");

    if (check_elf_globals_once() < 0) {
        return 1;
    }

    puts("parent ELF .data/.bss check OK\n");

    int children[CHILDREN];

    for (int i = 0; i < CHILDREN; i++) {
        int pid = fork();

        if (pid < 0) {
            puts("FAIL: fork failed\n");
            put_int(i);
            puts("\n");
            return 1;
        }

        if (pid == 0) {
            int code = child_main(i);

            if (code == 0) {
                log4("[child PASS]", i, getpid(), 0, 0);
            } else {
                log4("[child FAIL]", i, getpid(), code, 0);
            }

            exit(code);
        }

        children[i] = pid;
        log4("[parent forked]", i, pid, 0, getpid());
    }


    for (int i = 0; i < CHILDREN; i++) {
        int code = -1;
        int ret = waitpid(children[i], &code, 0);

        if (ret != children[i]) {
            log4("[parent wait bad pid]", i, ret, children[i], code);
            return 2;
        }

        if (code != 0) {
            log4("[parent child failed]", i, children[i], code, 0);
            return 3;
        }

        log4("[parent collected]", i, ret, code, 0);
    }

    puts("mega_elf_mmap_thread_fork_stress PASS\n");
    return 0;
}