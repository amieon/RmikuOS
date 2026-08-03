#include "user.h"

// 测试 ftruncate(65): 缩小(保留内容)/扩大(补零)/只读fd拒绝/坏fd
// tmpfs 全量测;/fat 挂了再补一组 FAT 持久化(关了重开看 size)。

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
static isize file_size(const char *path) { struct stat st; if (stat(path, &st) < 0) return -1; return (isize)st.st_size; }

int main(void) {
    puts("=== ftruncate test ===\n");

    puts("\n[0] setup\n");
    expect_ok("mkdir /tmp/fttest", mkdir("/tmp/fttest", 0777));

    puts("\n[1] ftruncate shrink & extend\n");
    isize fd = open_create("/tmp/fttest/a.txt", O_RDWR);
    expect_ok("create a.txt", fd);
    if (fd >= 0) {
        expect_eq("write 26 bytes", write(fd, "abcdefghijklmnopqrstuvwxyz", 26), 26);
        // 缩小到 10
        expect_eq("ftruncate to 10", ftruncate(fd, 10), 0);
        expect_eq("size == 10 after shrink", file_size("/tmp/fttest/a.txt"), 10);
        char buf[16];
        expect_eq("lseek(fd, 0, SET)", lseek(fd, 0, 0), 0);
        expect_eq("read returns 10", read(fd, buf, sizeof(buf)), 10);
        expect_eq("content[9] == 'j'", (isize)buf[9], (isize)'j');
        // 扩大到 20,新区域补 0
        expect_eq("ftruncate to 20", ftruncate(fd, 20), 0);
        expect_eq("size == 20 after extend", file_size("/tmp/fttest/a.txt"), 20);
        expect_eq("lseek(fd, 10, SET)", lseek(fd, 10, 0), 10);
        expect_eq("read tail 10 bytes", read(fd, buf, 10), 10);
        expect_eq("extended bytes are 0", (isize)buf[0], 0);
        close(fd);
    }
    // 只读 fd 上 ftruncate 应失败
    fd = open("/tmp/fttest/a.txt", O_RDONLY);
    if (fd >= 0) {
        expect_fail("ftruncate on O_RDONLY fd", ftruncate(fd, 5));
        close(fd);
    }
    expect_fail("ftruncate on bad fd", ftruncate(999, 0));

    if (path_exists("/fat")) {
        puts("\n[2] FAT: ftruncate persists across reopen\n");
        fd = open_create("/fat/ft_f.txt", O_RDWR);
        expect_ok("create /fat/ft_f.txt", fd);
        if (fd >= 0) {
            expect_eq("write 8 bytes", write(fd, "fatdata!", 8), 8);
            expect_eq("ftruncate to 3", ftruncate(fd, 3), 0);
            close(fd);
            expect_eq("FAT size == 3 after reopen", file_size("/fat/ft_f.txt"), 3);
            expect_ok("cleanup /fat/ft_f.txt", unlink("/fat/ft_f.txt"));
        }
    } else {
        puts("\n[2] /fat not mounted, skip FAT group\n");
    }

    puts("\n[cleanup]\n");
    expect_ok("remove_recursive /tmp/fttest", remove_recursive("/tmp/fttest"));

    puts("\n=== summary ===\n");
    puts("PASS: "); printf("%d", pass_count); puts("\n");
    puts("FAIL: "); printf("%d", fail_count); puts("\n");
    if (fail_count == 0) { puts("ALL TESTS PASSED\n"); return 0; }
    puts("SOME TESTS FAILED\n");
    return 1;
}
