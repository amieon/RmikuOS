#include "user.h"

// 测试 lseek(67): SET/CUR/END 语义 + 参数校验
// tmpfs 全量测;/fat 挂了再补一组 FAT 持久化测试(没盘跳过)。

static int pass_count = 0;
static int fail_count = 0;

static void expect_ok(const char *what, isize ret) {
    if (ret >= 0) { puts("  PASS: "); puts(what); puts("\n"); pass_count++; }
    else { puts("  FAIL: "); puts(what); puts(" (expected success, got "); printf("%d", (int)ret); puts(")\n"); fail_count++; }
}
static void expect_fail(const char *what, isize ret) {
    if (ret < 0) { puts("  PASS: "); puts(what); puts(" (correctly rejected)\n"); pass_count++; }
    else { puts("  FAIL: "); puts(what); puts(" (expected failure, but got "); printf("%d", (int)ret); puts(")\n"); fail_count++; }
}
static void expect_eq(const char *what, isize got, isize want) {
    if (got == want) { puts("  PASS: "); puts(what); puts("\n"); pass_count++; }
    else { puts("  FAIL: "); puts(what); puts(" (want "); printf("%d", (int)want); puts(", got "); printf("%d", (int)got); puts(")\n"); fail_count++; }
}
static int path_exists(const char *path) { struct stat st; return stat(path, &st) >= 0; }

int main(void) {
    puts("=== lseek test ===\n");

    puts("\n[0] setup\n");
    expect_ok("mkdir /tmp/lstest", mkdir("/tmp/lstest"));

    puts("\n[1] lseek SET/CUR/END\n");
    isize fd = open_create("/tmp/lstest/a.txt", O_RDWR);
    expect_ok("open_create /tmp/lstest/a.txt", fd);
    if (fd >= 0) {
        expect_eq("write 26 bytes", write(fd, "abcdefghijklmnopqrstuvwxyz", 26), 26);
        // SEEK_SET
        expect_eq("lseek(fd, 3, SET) == 3", lseek(fd, 3, 0), 3);
        char c;
        expect_eq("read 1 byte", read(fd, &c, 1), 1);
        expect_eq("byte at off 3 is 'd'", (isize)c, (isize)'d');
        // SEEK_CUR: 当前偏移 4, +2 => 6
        expect_eq("lseek(fd, 2, CUR) == 6", lseek(fd, 2, 1), 6);
        expect_eq("read 1 byte", read(fd, &c, 1), 1);
        expect_eq("byte at off 6 is 'g'", (isize)c, (isize)'g');
        // SEEK_END: 末尾-1 => 25
        expect_eq("lseek(fd, -1, END) == 25", lseek(fd, -1, 2), 25);
        expect_eq("read 1 byte", read(fd, &c, 1), 1);
        expect_eq("last byte is 'z'", (isize)c, (isize)'z');
        // 非法参数
        expect_fail("lseek(fd, 0, whence=9)", lseek(fd, 0, 9));
        expect_fail("lseek(fd, -100, SET)", lseek(fd, -100, 0));
        close(fd);
    }
    expect_fail("lseek on bad fd", lseek(999, 0, 0));

    if (path_exists("/fat")) {
        puts("\n[2] FAT: lseek@end + reopen still valid\n");
        fd = open_create("/fat/lseek_f.txt", O_RDWR);
        expect_ok("create /fat/lseek_f.txt", fd);
        if (fd >= 0) {
            expect_eq("write 8 bytes", write(fd, "fatdata!", 8), 8);
            expect_eq("lseek(fd, 0, END) == 8", lseek(fd, 0, 2), 8);
            expect_eq("lseek(fd, 0, SET) == 0", lseek(fd, 0, 0), 0);
            close(fd);
            fd = open("/fat/lseek_f.txt", O_RDWR);
            expect_eq("reopen lseek END == 8", lseek(fd, 0, 2), 8);
            close(fd);
            expect_ok("cleanup /fat/lseek_f.txt", unlink("/fat/lseek_f.txt"));
        }
    } else {
        puts("\n[2] /fat not mounted, skip FAT group\n");
    }

    puts("\n[cleanup]\n");
    expect_ok("remove_recursive /tmp/lstest", remove_recursive("/tmp/lstest"));

    puts("\n=== summary ===\n");
    puts("PASS: "); printf("%d", pass_count); puts("\n");
    puts("FAIL: "); printf("%d", fail_count); puts("\n");
    if (fail_count == 0) { puts("ALL TESTS PASSED\n"); return 0; }
    puts("SOME TESTS FAILED\n");
    return 1;
}
