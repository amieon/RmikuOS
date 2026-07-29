#include "user.h"

// 测试 rename(68): 同目录改名 / 跨目录移动 / 覆盖已存在文件(原子写) /
// 目录改名 / 错误路径(不存在源、移入自身子目录、跨设备 EXDEV)。

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
static void expect_exists(const char *path) {
    if (path_exists(path)) { puts("  PASS: exists "); puts(path); puts("\n"); pass_count++; }
    else { puts("  FAIL: should exist but missing: "); puts(path); puts("\n"); fail_count++; }
}
static void expect_absent(const char *path) {
    if (!path_exists(path)) { puts("  PASS: absent "); puts(path); puts("\n"); pass_count++; }
    else { puts("  FAIL: should be gone but exists: "); puts(path); puts("\n"); fail_count++; }
}
static isize file_size(const char *path) { struct stat st; if (stat(path, &st) < 0) return -1; return (isize)st.size; }

int main(void) {
    puts("=== rename test ===\n");

    puts("\n[0] setup\n");
    expect_ok("mkdir /tmp/rntest", mkdir("/tmp/rntest"));
    expect_ok("mkdir /tmp/rntest/sub", mkdir("/tmp/rntest/sub"));
    isize fd = open_create("/tmp/rntest/a.txt", O_RDWR);
    expect_ok("create a.txt", fd);
    if (fd >= 0) { expect_eq("write 5 bytes", write(fd, "hello", 5), 5); close(fd); }

    puts("\n[1] rename in same dir\n");
    expect_eq("rename a.txt -> b.txt", rename("/tmp/rntest/a.txt", "/tmp/rntest/b.txt"), 0);
    expect_absent("/tmp/rntest/a.txt");
    expect_exists("/tmp/rntest/b.txt");
    expect_eq("size kept after rename", file_size("/tmp/rntest/b.txt"), 5);

    puts("\n[2] rename across dirs (same fs)\n");
    expect_eq("move b.txt into sub/", rename("/tmp/rntest/b.txt", "/tmp/rntest/sub/c.txt"), 0);
    expect_absent("/tmp/rntest/b.txt");
    expect_exists("/tmp/rntest/sub/c.txt");

    puts("\n[3] rename overwrites existing file (atomic-write style)\n");
    fd = open_create("/tmp/rntest/victim", O_RDWR);
    if (fd >= 0) { write(fd, "old", 3); close(fd); }
    expect_eq("rename c.txt over victim", rename("/tmp/rntest/sub/c.txt", "/tmp/rntest/victim"), 0);
    expect_eq("victim now has new content (size 5)", file_size("/tmp/rntest/victim"), 5);
    expect_absent("/tmp/rntest/sub/c.txt");

    puts("\n[4] rename a directory\n");
    expect_eq("rename sub -> sub2", rename("/tmp/rntest/sub", "/tmp/rntest/sub2"), 0);
    expect_absent("/tmp/rntest/sub");
    expect_exists("/tmp/rntest/sub2");

    puts("\n[5] rename error cases\n");
    expect_fail("rename nonexistent src", rename("/tmp/rntest/nope", "/tmp/rntest/x"));
    expect_fail("rename dir into its own subdir", rename("/tmp/rntest", "/tmp/rntest/sub2/inside"));
    expect_fail("cross-device rename /tmp -> /fat", rename("/tmp/rntest/victim", "/fat/victim"));

    if (path_exists("/fat")) {
        puts("\n[6] FAT: rename on disk\n");
        fd = open_create("/fat/rn_f.txt", O_RDWR);
        expect_ok("create /fat/rn_f.txt", fd);
        if (fd >= 0) { write(fd, "x", 1); close(fd); }
        expect_eq("FAT rename", rename("/fat/rn_f.txt", "/fat/rn_g.txt"), 0);
        expect_absent("/fat/rn_f.txt");
        expect_exists("/fat/rn_g.txt");
        expect_ok("cleanup /fat/rn_g.txt", unlink("/fat/rn_g.txt"));
    } else {
        puts("\n[6] /fat not mounted, skip FAT group\n");
    }

    puts("\n[cleanup]\n");
    expect_ok("remove_recursive /tmp/rntest", remove_recursive("/tmp/rntest"));

    puts("\n=== summary ===\n");
    puts("PASS: "); printf("%d", pass_count); puts("\n");
    puts("FAIL: "); printf("%d", fail_count); puts("\n");
    if (fail_count == 0) { puts("ALL TESTS PASSED\n"); return 0; }
    puts("SOME TESTS FAILED\n");
    return 1;
}
