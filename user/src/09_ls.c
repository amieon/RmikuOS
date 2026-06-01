#include "user.h"

static void print_name(struct dirent *d) {
    for (int i = 0; i < d->name_len; i++) {
        char ch = d->name[i];
        write(1, &ch, 1);
    }

    if (d->file_type == FILE_TYPE_DIR) {
        puts("/");
    }

    puts("\n");
}

int main(int argc, char **argv) {
    const char *path = "/";

    if (argc >= 2) {
        path = argv[1];
    }

    int fd = open(path);

    if (fd < 0) {
        puts("ls: cannot open ");
        puts(path);
        puts("\n");
        return 1;
    }

    struct dirent entries[8];

    while (1) {
        isize n = getdents(fd, entries, sizeof(entries));

        if (n < 0) {
            puts("ls: getdents failed\n");
            close(fd);
            return 1;
        }

        if (n == 0) {
            break;
        }

        int count = n / sizeof(struct dirent);

        for (int i = 0; i < count; i++) {
            print_name(&entries[i]);
        }
    }

    close(fd);
    return 0;
}