#include "user.h"



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
                    put_int(r);
                    puts(" area=");
                    put_int(i);
                    puts(" offset=");
                    put_int(j);
                    puts("\n");
                    return 1;
                }
            }
        }

        for (int i = 0; i < N; i++) {
            if (munmap(ptrs[i], PAGE) < 0) {
                puts("FAIL: munmap failed\n");
                puts("round=");
                put_int(r);
                puts(" area=");
                put_int(i);
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