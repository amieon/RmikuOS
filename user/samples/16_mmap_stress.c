#include "user.h"



/* --- legacy append helpers (auto-injected, remove after refactor) --- */
static inline int append_str(char *buf, int pos, const char *s) {
    while (*s) buf[pos++] = *s++;
    return pos;
}
static inline int append_int(char *buf, int pos, int x) {
    char tmp[16]; int n = 0;
    if (x == 0) { buf[pos++] = '0'; return pos; }
    if (x < 0) { buf[pos++] = '-'; x = -x; }
    while (x > 0) { tmp[n++] = (char)('0' + x % 10); x /= 10; }
    while (n > 0) buf[pos++] = tmp[--n];
    return pos;
}
static inline int append_usize(char *buf, int pos, unsigned long x) {
    char tmp[24]; int n = 0;
    if (x == 0) { buf[pos++] = '0'; return pos; }
    while (x > 0) { tmp[n++] = (char)('0' + x % 10); x /= 10; }
    while (n > 0) buf[pos++] = tmp[--n];
    return pos;
}
/* --- end legacy append helpers --- */




#define N 32
#define PAGE 4096



static void print_progress(int round) {
    char buf[64];
    int pos = 0;

    pos = append_str(buf, pos, "[mmap_stress] round=");
    pos = append_int(buf, pos, round);
    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

int main(int argc, char *argv[]) {
    int rounds = 100;

    puts("mmap_stress start\n");

    for (int r = 0; r < rounds; r++) {
        char *ptrs[N];

        for (int i = 0; i < N; i++) {
            ptrs[i] = (char *)mmap(PAGE, PROT_READ | PROT_WRITE);

            if ((isize)ptrs[i] < 0) {
                puts("FAIL: mmap returned -1\n");
                return 1;
            }

            /*
             * 写入不同 pattern，检查不同 mmap 区域没有串。
             */
            for (int j = 0; j < PAGE; j++) {
                ptrs[i][j] = (char)(i + r + j);
            }
        }

        for (int i = 0; i < N; i++) {
            for (int j = 0; j < PAGE; j++) {
                char expected = (char)(i + r + j);

                if (ptrs[i][j] != expected) {
                    puts("FAIL: memory pattern mismatch\n");
                    puts("round=");
                    printf("%d", r);
                    puts(" area=");
                    printf("%d", i);
                    puts(" offset=");
                    printf("%d", j);
                    puts("\n");
                    return 1;
                }
            }
        }

        for (int i = 0; i < N; i++) {
            if (munmap(ptrs[i], PAGE) < 0) {
                puts("FAIL: munmap failed\n");
                puts("round=");
                printf("%d", r);
                puts(" area=");
                printf("%d", i);
                puts("\n");
                return 1;
            }
        }

        if (r % 10 == 0) {
            print_progress(r);
        }
    }

    puts("mmap_stress PASS\n");
    return 0;
}