#include"user.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        puts("cat: missing path\n");
        return 1;
    }


    int ret = 0;

    for (int argi = 1; argi < argc; argi++) {
        const char *path = argv[argi];

        int fd = open(path,O_RDONLY);
        if (fd < 0) {
            puts("cat: cannot open ");
            puts(path);
            puts("\n");
            ret = 1;
            continue;
        }

        char buf[128];

        while (1) {
            isize n = read(fd, buf, sizeof(buf));

            if (n < 0) {
                puts("cat: read failed: ");
                puts(path);
                puts("\n");
                ret = 1;
                break;
            }

            if (n == 0) {
                break;
            }

            write(1, buf, n);
        }

        close(fd);
    }

    return ret;
}