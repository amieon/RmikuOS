#include "user.h"



static void copy_dirent_name(struct dirent *d, char *out, int out_size) {
    int n = d->name_len;
    if (n > out_size - 1) {
        n = out_size - 1;
    }

    for (int i = 0; i < n; i++) {
        out[i] = d->name[i];
    }

    out[n] = 0;
}

static void join_path(const char *dir, const char *name, char *out, int out_size) {
    int pos = 0;

    if (dir[0] == '.' && dir[1] == 0) {
        for (int i = 0; name[i] && pos < out_size - 1; i++) {
            out[pos++] = name[i];
        }
        out[pos] = 0;
        return;
    }

    for (int i = 0; dir[i] && pos < out_size - 1; i++) {
        out[pos++] = dir[i];
    }

    if (pos > 0 && out[pos - 1] != '/' && pos < out_size - 1) {
        out[pos++] = '/';
    }

    for (int i = 0; name[i] && pos < out_size - 1; i++) {
        out[pos++] = name[i];
    }

    out[pos] = 0;
}


int main(int argc, char *argv[]) {

    const char *path = ".";

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

            puts("ls: not a directory: ");
            puts(path);
            puts("\n");
            close(fd);
            return 1;
        }

        if (n == 0) {
            break;
        }

        int count = n / sizeof(struct dirent);

        for (int i = 0; i < count; i++) {
            char name[64];
            char full_path[128];
            struct stat st;

            copy_dirent_name(&entries[i], name, sizeof(name));
            join_path(path, name, full_path, sizeof(full_path));

            if (stat(full_path, &st) < 0) {
                puts("?       ");
                puts(name);
                puts("\n");
                continue;
            }

            if (st.file_type == STAT_TYPE_DIR) {
                puts("dir     ");
            } else if (st.file_type == STAT_TYPE_FILE) {
                puts("file    ");
            } else if (st.file_type == STAT_TYPE_CHAR) {
                puts("char    ");
            } else {
                puts("unknown ");
            }

            put_int(st.size);
            puts(" ");

            puts(name);
            if (st.file_type == STAT_TYPE_DIR) {
                puts("/");
            }
            puts("\n");
        }
    }

    close(fd);
    return 0;
}