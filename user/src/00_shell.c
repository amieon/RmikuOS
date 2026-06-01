#include "user.h"

#define LINE_SIZE 128
#define MAX_ARGC  8

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int read_line(char *buf, int max_len) {
    int len = 0;

    while (len < max_len - 1) {
        char ch = 0;
        isize n = read(0, &ch, 1);

        if (n <= 0) {
            continue;
        }

        if (ch == '\r') {
            ch = '\n';
        }

        if (ch == '\n') {
            puts("\n");
            break;
        }

        if (ch == 8 || ch == 127) {
            if (len > 0) {
                len--;
                write(1, "\b \b", 3);
            }
            continue;
        }

        buf[len++] = ch;
        write(1, &ch, 1);
    }

    buf[len] = 0;
    return len;
}

static int parse_args(char *line, char *argv[], int max_argc) {
    int argc = 0;
    int i = 0;

    while (line[i]) {
        while (line[i] == ' ' || line[i] == '\t') {
            line[i] = 0;
            i++;
        }

        if (!line[i]) {
            break;
        }

        if (argc >= max_argc) {
            break;
        }

        argv[argc++] = &line[i];

        while (line[i] && line[i] != ' ' && line[i] != '\t') {
            i++;
        }
    }

    return argc;
}

static void print_help(void) {
    puts("commands:\n");
    puts("  help\n");
    puts("  exit\n");
    puts("  ls [path]\n");
    puts("  cat <path>\n");
    puts("\n");
    puts("external commands are in /bin:\n");
    puts("  try: ls /bin\n");
}

static void print_dirent_name(struct dirent *d) {
    for (int i = 0; i < d->name_len; i++) {
        char ch = d->name[i];
        write(1, &ch, 1);
    }

    if (d->file_type == FILE_TYPE_DIR) {
        puts("/");
    }

    puts("\n");
}

static int builtin_ls(int argc, char *argv[]) {
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
            print_dirent_name(&entries[i]);
        }
    }

    close(fd);
    return 0;
}

static int builtin_cat(int argc, char *argv[]) {
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

static void build_exec_path(const char *cmd, char *out, int out_size) {
    if (cmd[0] == '/') {
        int i = 0;
        while (cmd[i] && i < out_size - 1) {
            out[i] = cmd[i];
            i++;
        }
        out[i] = 0;
        return;
    }

    const char *prefix = "/bin/";
    int pos = 0;

    for (int i = 0; prefix[i] && pos < out_size - 1; i++) {
        out[pos++] = prefix[i];
    }

    for (int i = 0; cmd[i] && pos < out_size - 1; i++) {
        out[pos++] = cmd[i];
    }

    out[pos] = 0;
}

static void run_external(int argc, char *argv[]) {
    isize pid = fork();

    if (pid == 0) {
        char path[96];

        build_exec_path(argv[0], path, sizeof(path));

        struct exec_args args;
        args.argc = argc;

        for (int i = 0; i < EXEC_MAX_ARGS; i++) {
            args.argv[i].ptr = 0;
            args.argv[i].len = 0;
        }

        for (int i = 0; i < argc && i < EXEC_MAX_ARGS; i++) {
            args.argv[i].ptr = argv[i];
            args.argv[i].len = strlen(argv[i]);
        }

        exec_with_args(path, &args);

        puts("exec failed: ");
        puts(path);
        puts("\n");

        exit(1);
    } else if (pid > 0) {
        int code = -1;
        waitpid(pid, &code);

        puts("[shell] child exit code ");
        put_int(code);
        puts("\n");
    } else {
        puts("fork failed\n");
    }
}
int main(void) {
    char line[LINE_SIZE];
    char *argv[MAX_ARGC];

    puts("\nRmikuOS shell\n");
    print_help();

    while (1) {
        puts("\n$ ");

        int len = read_line(line, LINE_SIZE);

        if (len == 0) {
            continue;
        }

        int argc = parse_args(line, argv, MAX_ARGC);

        if (argc == 0) {
            continue;
        }

        if (streq(argv[0], "help")) {
            print_help();
            continue;
        }

        if (streq(argv[0], "exit")) {
            puts("bye\n");
            return 0;
        }

        if (streq(argv[0], "ls")) {
            int code = builtin_ls(argc, argv);
            puts("[shell] builtin exit code ");
            put_int(code);
            puts("\n");
            continue;
        }

        if (streq(argv[0], "cat")) {
            int code = builtin_cat(argc, argv);
            puts("[shell] builtin exit code ");
            put_int(code);
            puts("\n");
            continue;
        }

        run_external(argc, argv);
    }

    return 0;
}