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

    int fd = open(path,O_RDONLY);
    if (fd < 0) {
        fputs("ls: cannot open ", stdout);
        fputs(path, stdout);
        fputs("\n", stdout);
        fflush(stdout);
        return 1;
    }

    struct dirent entries[8];

    while (1) {
        isize n = getdents(fd, entries, sizeof(entries));

        if (n < 0) {

            fputs("ls: not a directory: ", stdout);
            fputs(path, stdout);
            fputs("\n", stdout);
            fflush(stdout);
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
                fputs("?       ", stdout);
                fputs(name, stdout);
                fputs("\n", stdout);
                fflush(stdout);
                continue;
            }

            if (stat_type_of(st.st_mode) == STAT_TYPE_DIR) {
                fputs("dir     ", stdout);
            } else if (stat_type_of(st.st_mode) == STAT_TYPE_FILE) {
                fputs("file    ", stdout);
            } else if (stat_type_of(st.st_mode) == STAT_TYPE_CHAR) {
                fputs("char    ", stdout);
            } else {
                fputs("unknown ", stdout);
            }

            printf("%d", st.st_size);
            fputs(" ", stdout);

            fputs(name, stdout);
            if (stat_type_of(st.st_mode) == STAT_TYPE_DIR) {
                fputs("/", stdout);
            }
            fputs("\n", stdout);
            fflush(stdout);
        }
    }

    close(fd);
    return 0;
}