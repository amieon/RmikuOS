#include "user.h"
#include "malloc.h"

int main() {
    puts("malloc_test start\n");

    char *a = (char *)malloc(32);
    char *b = (char *)malloc(4096);
    char *c = (char *)malloc(70000);

    if (!a || !b || !c) {
        puts("FAIL: malloc returned null\n");
        return 1;
    }

    a[0] = 'R';
    a[1] = 'm';
    a[2] = 'i';
    a[3] = 'k';
    a[4] = 'u';
    a[5] = '\n';

    write(1, a, 6);

    for (int i = 0; i < 4096; i++) {
        b[i] = (char)(i & 0xff);
    }

    for (int i = 0; i < 4096; i++) {
        if (b[i] != (char)(i & 0xff)) {
            puts("FAIL: b pattern mismatch\n");
            return 1;
        }
    }

    for (int i = 0; i < 70000; i++) {
        c[i] = (char)((i * 7) & 0xff);
    }

    for (int i = 0; i < 70000; i++) {
        if (c[i] != (char)((i * 7) & 0xff)) {
            puts("FAIL: c pattern mismatch\n");
            return 1;
        }
    }

    free(a);
    free(b);
    free(c);

    char *d = (char *)malloc(16);

    if (!d) {
        puts("FAIL: malloc after free failed\n");
        return 1;
    }

    d[0] = 'O';
    d[1] = 'K';
    d[2] = '\n';

    write(1, d, 3);

    puts("malloc_test PASS\n");
    return 0;
}