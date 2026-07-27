#include "user.h"

int main(void) {
    struct stat st;

    if (stat("/etc/motd", &st) < 0) {
        puts("stat failed\n");
        return 1;
    }

    puts("/etc/motd type=");
    printf("%d", st.file_type);
    puts(" size=");
    printf("%d", st.size);
    puts("\n");

    int fd = open("/etc/motd",O_RDWR);
    if (fd < 0) {
        puts("open failed\n");
        return 1;
    }

    if (fstat(fd, &st) < 0) {
        puts("fstat failed\n");
        close(fd);
        return 1;
    }

    puts("fd stat type=");
    printf("%d", st.file_type);
    puts(" size=");
    printf("%d", st.size);
    puts("\n");

    close(fd);
    return 0;
}