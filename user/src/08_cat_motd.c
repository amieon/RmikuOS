#include "user.h"

int main(void) {
    char buf[64];

    int fd = open("/etc/motd");

    if (fd < 0) {
        puts("open /etc/motd failed\n");
        return 1;
    }

    while (1) {
        isize n = read(fd, buf, sizeof(buf));

        if (n < 0) {
            puts("read failed\n");
            close(fd);
            return 1;
        }

        if (n == 0) {
            break;
        }

        write(1, buf, n);
    }

    close(fd);
    return 0;
}