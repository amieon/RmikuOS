#include "user.h"

void _start(void) {
    puts("[busy.c] start\n");

    volatile unsigned long i = 0;
    while (i < 20000000UL) {
        i++;
    }

    puts("[busy.c] end\n");
    exit(1);
}