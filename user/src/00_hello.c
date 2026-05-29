#include "user.h"

void _start(void) {
    puts("[hello.c] before yield\n");
    yield();
    puts("[hello.c] after yield\n");
    exit(0);
}