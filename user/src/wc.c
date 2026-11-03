/* wc —— 数行 / 词 / 字节。
 *
 * 选项对齐 POSIX：-l 行数、-w 词数、-c 字节数，可组合（-lwc）。
 * 不给文件读 stdin；多文件时末尾补一行 total（POSIX 行为）。
 * 不做列宽右对齐（%7ld），省得依赖 printf 的宽度支持。
 */
#include "user.h"
#include "ctype.h"

#define WC_BUFSZ 1024

struct wc_count {
    long lines, words, bytes;
};

/* 统计一个 fd 到 *c。返回 0 成功，-1 读失败（*c 里已累计数保留）。 */
static int wc_stream(int fd, struct wc_count *c) {
    char buf[WC_BUFSZ];
    int in_word = 0;

    c->lines = 0;
    c->words = 0;
    c->bytes = 0;

    while (1) {
        isize n = read(fd, buf, sizeof(buf));
        if (n < 0) return -1;
        if (n == 0) break;
        c->bytes += (long)n;
        for (isize i = 0; i < n; i++) {
            unsigned char ch = (unsigned char)buf[i];
            if (ch == '\n') c->lines++;
            if (isspace(ch)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                c->words++;
            }
        }
    }
    return 0;
}

static void usage(void) {
    fputs("usage: wc [-lwc] [file...]\n", stderr);
}

int main(int argc, char *argv[]) {
    int do_l = 0, do_w = 0, do_c = 0;
    int nfiles = 0, ret = 0;
    int i;

    /* 解析选项。第一个非选项参数之后全是文件名（POSIX：允许 `wc -l f`）。 */
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0') break;
        if (strcmp(a, "--help") == 0) {
            usage();
            return 0;
        }
        for (int j = 1; a[j] != '\0'; j++) {
            if (a[j] == 'l') do_l = 1;
            else if (a[j] == 'w') do_w = 1;
            else if (a[j] == 'c') do_c = 1;
            else {
                fputs("wc: invalid option -- ", stderr);
                fputc(a[j], stderr);
                fputs("\n", stderr);
                usage();
                return 1;
            }
        }
    }

    /* 一个选项都没给 → 三个都打 */
    if (!do_l && !do_w && !do_c) do_l = do_w = do_c = 1;

    int first_file = i;
    struct wc_count total = {0, 0, 0};

    if (first_file >= argc) {
        /* stdin */
        struct wc_count c;
        if (wc_stream(0, &c) < 0) {
            fputs("wc: read failed\n", stderr);
            return 1;
        }
        if (do_l) { printf("%ld", c.lines); if (do_w || do_c) printf(" "); }
        if (do_w) { printf("%ld", c.words); if (do_c) printf(" "); }
        if (do_c) printf("%ld", c.bytes);
        printf("\n");
        return 0;
    }

    for (i = first_file; i < argc; i++) {
        const char *path = argv[i];
        struct wc_count c;
        int fd = open(path, O_RDONLY);
        nfiles++;
        if (fd < 0) {
            fputs("wc: cannot open ", stderr);
            fputs(path, stderr);
            fputs("\n", stderr);
            ret = 1;
            continue;
        }
        if (wc_stream(fd, &c) < 0) {
            fputs("wc: read failed: ", stderr);
            fputs(path, stderr);
            fputs("\n", stderr);
            ret = 1;
            close(fd);
            continue;
        }
        close(fd);

        total.lines += c.lines;
        total.words += c.words;
        total.bytes += c.bytes;

        if (do_l) { printf("%ld", c.lines); if (do_w || do_c) printf(" "); }
        if (do_w) { printf("%ld", c.words); if (do_c) printf(" "); }
        if (do_c) printf("%ld", c.bytes);
        printf(" %s\n", path);
    }

    /* 只有多个文件才补 total；单文件时那是噪声（POSIX 也是这么定的） */
    if (nfiles > 1) {
        if (do_l) { printf("%ld", total.lines); if (do_w || do_c) printf(" "); }
        if (do_w) { printf("%ld", total.words); if (do_c) printf(" "); }
        if (do_c) printf("%ld", total.bytes);
        printf(" total\n");
    }

    return ret;
}
