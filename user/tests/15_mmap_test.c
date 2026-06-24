#include "user.h"

int main(int argc, char *argv[]) {
    puts("mmap_test start\n");

    char *p = mmap(4096, PROT_READ | PROT_WRITE);

    if ((isize)p < 0) {
        puts("mmap failed\n");
        return 1;
    }

    p[0] = 'R';
    p[1] = 'm';
    p[2] = 'i';
    p[3] = 'k';
    p[4] = 'u';
    p[5] = '\n';

    write(1, p, 6);

    if (munmap(p, 4096) < 0) {
        puts("munmap failed\n");
        return 1;
    }

    puts("mmap_test PASS\n");
    return 0;
}