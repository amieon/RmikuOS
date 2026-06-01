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

int main(void) {
    int fd = open("/bin");

    if (fd < 0) {
        puts("ls_bin: open failed\n");
        return 1;
    }

    struct dirent entries[8];

    while (1) {
        isize n = getdents(fd, entries, sizeof(entries));

        if (n < 0) {
            puts("ls_bin: getdents failed\n");
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