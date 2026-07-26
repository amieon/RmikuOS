#include "user.h"

int main(void) {
    puts("[getpid_sleep] start, pid=");
    printf("%d", getpid());
    puts("\n");

    puts("[getpid_sleep] sleep 5 ticks\n");
    sleep(5);

    puts("[getpid_sleep] wake, pid=");
    printf("%d", getpid());
    puts("\n");

    exit(10);
}