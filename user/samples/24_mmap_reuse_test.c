#include "user.h"

#ifndef PROT_READ
#define PROT_READ  1
#define PROT_WRITE 2
#endif

#define PAGE 4096

int main() {
    puts("mmap_reuse_test start\n");

    char *a = (char *)mmap(PAGE, PROT_READ | PROT_WRITE);
    char *b = (char *)mmap(PAGE, PROT_READ | PROT_WRITE);
    char *c = (char *)mmap(PAGE, PROT_READ | PROT_WRITE);

    if ((isize)a < 0 || (isize)b < 0 || (isize)c < 0) {
        puts("FAIL: initial mmap\n");
        return 1;
    }

    a[0] = 'A';
    b[0] = 'B';
    c[0] = 'C';

    if (munmap(b, PAGE) < 0) {
        puts("FAIL: munmap b\n");
        return 1;
    }

    char *d = (char *)mmap(PAGE, PROT_READ | PROT_WRITE);

    if ((isize)d < 0) {
        puts("FAIL: mmap d\n");
        return 1;
    }

    puts("b=");
    put_int((usize)b);
    puts(" d=");
    put_int((usize)d);
    puts("\n");

    if (d != b) {
        puts("FAIL: mmap did not reuse freed range\n");
        return 1;
    }

    d[0] = 'D';

    if (a[0] != 'A' || c[0] != 'C' || d[0] != 'D') {
        puts("FAIL: memory corrupted\n");
        return 1;
    }

    munmap(a, PAGE);
    munmap(c, PAGE);
    munmap(d, PAGE);

    puts("mmap_reuse_test PASS\n");
    return 0;
}