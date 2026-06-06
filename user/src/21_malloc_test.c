#include "user.h"
#include "malloc.h"

#define N 160
#define ROUNDS 150


static void progress(int round) {
    char buf[80];
    int pos = 0;

    pos = append_str(buf, pos, "[malloc_stress] round=");
    pos = append_int(buf, pos, round);
    pos = append_str(buf, pos, "\n");

    write(1, buf, pos);
}

static unsigned char pat(int round, int i, int j) {
    unsigned int x = 0x9e3779b9u;

    x ^= (unsigned int)round * 1103515245u;
    x ^= (unsigned int)i * 2654435761u;
    x ^= (unsigned int)j * 97u;

    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;

    return (unsigned char)(x & 0xff);
}

int main() {
    puts("malloc_stress start\n");

    void *ptrs[N];
    usize sizes[N];

    for (int r = 0; r < ROUNDS; r++) {
        for (int i = 0; i < N; i++) {
            /*
             * 混合小块、中块、偶尔大块。
             */
            usize size;

            if (i % 31 == 0) {
                size = 70000 + (usize)((r + i) % 4096);
            } else if (i % 7 == 0) {
                size = 4096 + (usize)((r * 13 + i * 17) % 8192);
            } else {
                size = 1 + (usize)((r * 37 + i * 53) % 2048);
            }

            sizes[i] = size;
            ptrs[i] = malloc(size);

            if (!ptrs[i]) {
                puts("FAIL: malloc null\n");
                puts("round=");
                put_int(r);
                puts(" i=");
                put_int(i);
                puts(" size=");
                put_int(size);
                puts("\n");
                return 1;
            }

            char *p = (char *)ptrs[i];

            for (usize j = 0; j < size; j++) {
                p[j] = (char)pat(r, i, (int)j);
            }
        }

        /*
         * 验证所有块没有互相踩。
         */
        for (int i = 0; i < N; i++) {
            char *p = (char *)ptrs[i];

            for (usize j = 0; j < sizes[i]; j++) {
                char expected = (char)pat(r, i, (int)j);

                if (p[j] != expected) {
                    puts("FAIL: pattern mismatch\n");
                    puts("round=");
                    put_int(r);
                    puts(" i=");
                    put_int(i);
                    puts(" offset=");
                    put_int(j);
                    puts("\n");
                    return 1;
                }
            }
        }

        /*
         * 交错 free，制造碎片。
         */
        for (int i = 0; i < N; i += 2) {
            free(ptrs[i]);
            ptrs[i] = 0;
        }

        /*
         * 再申请一批，测试 free block 复用和 split。
         */
        for (int i = 0; i < N; i += 2) {
            usize size = 16 + (usize)((r * 19 + i * 23) % 3072);

            ptrs[i] = malloc(size);
            sizes[i] = size;

            if (!ptrs[i]) {
                puts("FAIL: second malloc null\n");
                puts("round=");
                put_int(r);
                puts(" i=");
                put_int(i);
                puts("\n");
                return 1;
            }

            char *p = (char *)ptrs[i];

            for (usize j = 0; j < size; j++) {
                p[j] = (char)pat(r + 1000, i, (int)j);
            }
        }

        for (int i = 0; i < N; i += 2) {
            char *p = (char *)ptrs[i];

            for (usize j = 0; j < sizes[i]; j++) {
                char expected = (char)pat(r + 1000, i, (int)j);

                if (p[j] != expected) {
                    puts("FAIL: second pattern mismatch\n");
                    puts("round=");
                    put_int(r);
                    puts(" i=");
                    put_int(i);
                    puts(" offset=");
                    put_int(j);
                    puts("\n");
                    return 1;
                }
            }
        }

        for (int i = 0; i < N; i++) {
            free(ptrs[i]);
            ptrs[i] = 0;
        }

        if (r % 10 == 0) {
            progress(r);
        }
    }

    puts("malloc_stress PASS\n");
    return 0;
}