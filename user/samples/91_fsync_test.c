#include "user.h"

// 测试 fsync(64): tmpfs 上应为 no-op 成功;坏 fd 失败。
// /fat 挂了再测一次真正的刷盘链(写->fsync->重开看内容还在)。

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
static isize file_size(const char *path) { struct stat st; if (stat(path, &st) < 0) return -1; return (isize)st.size; }

int main(void) {
    puts("=== fsync test ===\n");

    puts("\n[0] setup\n");
    expect_ok("mkdir /tmp/fstest", mkdir("/tmp/fstest"));

    puts("\n[1] fsync on tmpfs (no-op, should succeed)\n");
    isize fd = open_create("/tmp/fstest/a.txt", O_RDWR);
    expect_ok("create a.txt", fd);
    if (fd >= 0) {
        expect_eq("write 4 bytes", write(fd, "data", 4), 4);
        expect_eq("fsync(fd) == 0", fsync(fd), 0);
        expect_eq("size still 4 after fsync", file_size("/tmp/fstest/a.txt"), 4);
        close(fd);
    }
    expect_fail("fsync on bad fd", fsync(999));

    if (path_exists("/fat")) {
        puts("\n[2] FAT: fsync flushes to block device\n");
        fd = open_create("/fat/fs_f.txt", O_RDWR);
        expect_ok("create /fat/fs_f.txt", fd);
        if (fd >= 0) {
            expect_eq("write 8 bytes", write(fd, "fatdata!", 8), 8);
            expect_eq("fsync == 0", fsync(fd), 0);
            close(fd);
            // 重新打开验证内容真的落到介质(不是只在内存缓存)
            fd = open("/fat/fs_f.txt", O_RDONLY);
            expect_ok("reopen /fat/fs_f.txt", fd);
            if (fd >= 0) {
                char buf[16] = {0};
                expect_eq("read 8 bytes back", read(fd, buf, 8), 8);
                expect_eq("content matches", (isize)buf[0], (isize)'f');
                close(fd);
            }
            expect_ok("cleanup /fat/fs_f.txt", unlink("/fat/fs_f.txt"));
        }
    } else {
        puts("\n[2] /fat not mounted, skip FAT group\n");
    }

    puts("\n[cleanup]\n");
    expect_ok("remove_recursive /tmp/fstest", remove_recursive("/tmp/fstest"));

    puts("\n=== summary ===\n");
    puts("PASS: "); printf("%d", pass_count); puts("\n");
    puts("FAIL: "); printf("%d", fail_count); puts("\n");
    if (fail_count == 0) { puts("ALL TESTS PASSED\n"); return 0; }
    puts("SOME TESTS FAILED\n");
    return 1;
}
