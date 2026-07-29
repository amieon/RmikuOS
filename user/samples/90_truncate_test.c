#include "user.h"

// 测试 truncate(path, len)(66): 路径版截断
// 正常截断 / 不存在路径失败 / 对目录应失败。

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
static isize file_size(const char *path) { struct stat st; if (stat(path, &st) < 0) return -1; return (isize)st.size; }

int main(void) {
    puts("=== truncate(path) test ===\n");

    puts("\n[0] setup\n");
    expect_ok("mkdir /tmp/trtest", mkdir("/tmp/trtest"));
    isize fd = open_create("/tmp/trtest/a.txt", O_RDWR);
    expect_ok("create a.txt", fd);
    if (fd >= 0) { expect_eq("write 26 bytes", write(fd, "abcdefghijklmnopqrstuvwxyz", 26), 26); close(fd); }
    expect_ok("mkdir /tmp/trtest/sub", mkdir("/tmp/trtest/sub"));

    puts("\n[1] truncate by path\n");
    expect_eq("truncate a.txt to 5", truncate("/tmp/trtest/a.txt", 5), 0);
    expect_eq("size == 5", file_size("/tmp/trtest/a.txt"), 5);
    expect_fail("truncate nonexistent", truncate("/tmp/trtest/nope", 0));
    expect_fail("truncate a directory", truncate("/tmp/trtest/sub", 0));

    puts("\n[cleanup]\n");
    expect_ok("remove_recursive /tmp/trtest", remove_recursive("/tmp/trtest"));

    puts("\n=== summary ===\n");
    puts("PASS: "); printf("%d", pass_count); puts("\n");
    puts("FAIL: "); printf("%d", fail_count); puts("\n");
    if (fail_count == 0) { puts("ALL TESTS PASSED\n"); return 0; }
    puts("SOME TESTS FAILED\n");
    return 1;
}
