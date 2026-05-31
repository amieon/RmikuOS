#include "user.h"

#define LINE_SIZE 64

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

        if (ch == 4) {
            return -1;
        }

        if (ch == 3) {
            puts("^C\n");
            buf[0] = 0;
            return 0;
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

static void print_help(void) {
    puts("commands:\n");
    puts("  help\n");
    puts("  exit\n");
    puts("  hello\n");
    puts("  busy\n");
    puts("  getpid_sleep\n");
    puts("  fork_wait\n");
}

int main(void) {
    char line[LINE_SIZE];

    puts("\nRmikuOS shell\n");
    print_help();

    while (1) {
        puts("\n$ ");

        int len = read_line(line, LINE_SIZE);

        if (len < 0) {
            puts("\n");
            continue;
        }

        if (len == 0) {
            continue;
        }

        if (streq(line, "help")) {
            print_help();
            continue;
        }

        if (streq(line, "exit")) {
            puts("bye\n");
            return 0;
        }

        isize pid = fork();

        if (pid == 0) {
            exec2(line, strlen(line));

            puts("exec failed: ");
            puts(line);
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

    return 0;
}