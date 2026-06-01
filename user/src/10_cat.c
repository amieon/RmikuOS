#include "user.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("cat: missing path\n");
        return 1;
    }

    const char *path = argv[1];

    int fd = open(path);

    if (fd < 0) {
        puts("cat: cannot open ");
        puts(path);
        puts("\n");
        return 1;
    }

    char buf[128];

    while (1) {
        isize n = read(fd, buf, sizeof(buf));

        if (n < 0) {
            puts("cat: read failed\n");
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