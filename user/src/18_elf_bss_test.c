#include "user.h"

static int global_zero;
static char big_bss[16384];

static int global_init = 1234;
static char data_buf[4] = { 'R', 'm', 'i', 'k' };

int main() {
    puts("elf_bss_test start\n");

    if (global_zero != 0) {
        puts("FAIL: global_zero not zero\n");
        return 1;
    }

    for (int i = 0; i < sizeof(big_bss); i++) {
        if (big_bss[i] != 0) {
            puts("FAIL: bss not zero\n");
            return 1;
        }
    }

    if (global_init != 1234) {
        puts("FAIL: data global wrong\n");
        return 1;
    }

    write(1, data_buf, 4);
    puts("\n");

    big_bss[0] = 'O';
    big_bss[1] = 'K';
    big_bss[2] = '\n';
    write(1, big_bss, 3);

    puts("elf_bss_test PASS\n");
    return 0;
}