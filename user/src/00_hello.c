#include "user.h"

int main(void) {
    puts("[hello.c] before yield\n");
    yield();
    puts("[hello.c] after yield\n");
    exit(0);
}