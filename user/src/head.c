/* head —— 输出每个文件的前 N 行（默认 10）。
 *
 * 支持 `-n N` 和老的 `-N` 写法（head -5 file），两者 GNU/POSIX 都认。
 * 不给文件读 stdin；多文件时按 GNU 习惯打 `==> name <==` 分隔，文件之间空一行。
 * 逐块读、数到第 N 个 '\n' 就停，不会把整个文件读进来（日志可能很大）。
 */
#include "user.h"

#define HEAD_BUFSZ 1024

/* 把 fd 的前 maxlines 行写到 stdout。返回 0 成功，-1 读失败。 */
static int head_stream(int fd, long maxlines) {
    char buf[HEAD_BUFSZ];
    long seen = 0;

    if (maxlines <= 0) return 0;

    while (1) {
        isize n = read(fd, buf, sizeof(buf));
        if (n < 0) return -1;
        if (n == 0) return 0;

        for (isize i = 0; i < n; i++) {
            if (buf[i] != '\n') continue;
            seen++;
            if (seen == maxlines) {
                /* 连同这个换行一起写出，剩下的不读 */
                isize len = i + 1;
                isize off = 0;
                while (off < len) {
                    isize w = write(1, buf + off, (size_t)(len - off));
                    if (w <= 0) return -1;
                    off += w;
                }
                return 0;
            }
        }

        isize off = 0;
        while (off < n) {
            isize w = write(1, buf + off, (size_t)(n - off));
            if (w <= 0) return -1;
            off += w;
        }
    }
}

/* 解析十进制，顺便挡掉 "abc" 和非正数。返回 -1 表示非法。 */
static long parse_long(const char *s) {
    long v = 0;
    int digits = 0;

    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        digits++;
        s++;
        if (v > 1000000) return -1;   /* 荒谬值直接拒，防溢出 */
    }
    if (!digits || *s != '\0') return -1;
    return v;
}

static void usage(void) {
    fputs("usage: head [-n N] [file...]\n", stderr);
}

int main(int argc, char *argv[]) {
    long maxlines = 10;
    int i = 1, ret = 0;

    while (i < argc) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;

        if (strcmp(a, "--help") == 0) {
            usage();
            return 0;
        }
        if (a[1] == 'n') {
            const char *num;
            if (a[2] != '\0') {
                num = a + 2;            /* -n5 紧贴写法 */
            } else if (i + 1 < argc) {
                num = argv[++i];        /* -n 5 */
            } else {
                fputs("head: option requires an argument -- n\n", stderr);
                usage();
                return 1;
            }
            maxlines = parse_long(num);
            if (maxlines < 0) {
                fputs("head: invalid number of lines: ", stderr);
                fputs(num, stderr);
                fputs("\n", stderr);
                return 1;
            }
            i++;
            continue;
        }
        /* 老写法 -5 */
        maxlines = parse_long(a + 1);
        if (maxlines < 0) {
            fputs("head: invalid option -- ", stderr);
            fputs(a + 1, stderr);
            fputs("\n", stderr);
            usage();
            return 1;
        }
        i++;
    }

    if (i >= argc) {
        if (head_stream(0, maxlines) < 0) {
            fputs("head: read failed\n", stderr);
            return 1;
        }
        return 0;
    }

    int nfiles = argc - i;
    for (int fi = i; fi < argc; fi++) {
        const char *path = argv[fi];
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            fputs("head: cannot open ", stderr);
            fputs(path, stderr);
            fputs("\n", stderr);
            ret = 1;
            continue;
        }
        if (nfiles > 1) {
            if (fi > i) printf("\n");
            printf("==> %s <==\n", path);
        }
        if (head_stream(fd, maxlines) < 0) {
            fputs("head: read failed: ", stderr);
            fputs(path, stderr);
            fputs("\n", stderr);
            ret = 1;
        }
        close(fd);
    }

    return ret;
}
