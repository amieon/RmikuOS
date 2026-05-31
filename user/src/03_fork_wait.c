#include "user.h"

void _start(void) {
    puts("[getpid_sleep] start, pid=");
    put_int(getpid());
    puts("\n");

    puts("[getpid_sleep] sleep 5 ticks\n");
    sleep(5);

    puts("[getpid_sleep] wake, pid=");
    put_int(getpid());
    puts("\n");

    exit(10);
}