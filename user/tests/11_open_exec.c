#include "user.h"

int main(void) {
    int fd = open("/etc/motd",O_RDWR);

    if (fd < 0) {
        puts("open failed\n");
        return 1;
    }

    puts("opened fd=");
    put_int(fd);
    puts(", now exec hello\n");

    exec("hello");

    puts("exec failed\n");
    return 1;
}