#include "user.h"

// 测试可写 tmpfs 的完整 CRUD + 删除语义
// 每个用例:执行操作,检查返回值是否符合预期,打印 PASS/FAIL

static int pass_count = 0;
static int fail_count = 0;

// 检查"返回值应该成功(>=0)"
static void expect_ok(const char *what, isize ret) {
    if (ret >= 0) {
        puts("  PASS: "); puts(what); puts("\n");
        pass_count++;
    } else {
        puts("  FAIL: "); puts(what); puts(" (expected success, got error)\n");
        fail_count++;
    }
}

// 检查"返回值应该失败(<0)"
static void expect_fail(const char *what, isize ret) {
    if (ret < 0) {
        puts("  PASS: "); puts(what); puts(" (correctly rejected)\n");
        pass_count++;
    } else {
        puts("  FAIL: "); puts(what); puts(" (expected failure, but succeeded)\n");
        fail_count++;
    }
}

// 检查路径是否存在(用 stat),返回 1 存在 0 不存在
static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) >= 0;
}

static void expect_exists(const char *path) {
    if (path_exists(path)) {
        puts("  PASS: exists "); puts(path); puts("\n");
        pass_count++;
    } else {
        puts("  FAIL: should exist but missing: "); puts(path); puts("\n");
        fail_count++;
    }
}

static void expect_absent(const char *path) {
    if (!path_exists(path)) {
        puts("  PASS: absent "); puts(path); puts("\n");
        pass_count++;
    } else {
        puts("  FAIL: should be gone but still exists: "); puts(path); puts("\n");
        fail_count++;
    }
}

int main(void) {
    puts("=== tmpfs CRUD + delete semantics test ===\n");

    // --- 1. 创建目录树 ---
    puts("\n[1] build directory tree\n");
    expect_ok("mkdir /tmp/haha", mkdir("/tmp/haha"));
    expect_ok("mkdir /tmp/haha/nihao", mkdir("/tmp/haha/nihao"));
    expect_ok("touch /tmp/haha/file1", create2("/tmp/haha/file1", strlen("/tmp/haha/file1")));
    expect_exists("/tmp/haha");
    expect_exists("/tmp/haha/nihao");
    expect_exists("/tmp/haha/file1");

    // --- 2. 文件读写 ---
    puts("\n[2] file write/read\n");
    isize fd = open_create("/tmp/haha/data");
    expect_ok("open_create /tmp/haha/data", fd);
    if (fd >= 0) {
        isize w = write(fd, "hello tmpfs", 11);
        expect_ok("write 11 bytes", w >= 0 ? w : -1);
        close(fd);

        isize fd2 = open("/tmp/haha/data");
        char buf[32];
        isize n = read(fd2, buf, sizeof(buf));
        close(fd2);
        if (n == 11 && buf[0] == 'h' && buf[10] == 's') {
            puts("  PASS: read back content correct\n");
            pass_count++;
        } else {
            puts("  FAIL: read back content wrong, n=");
            put_int(n); puts("\n");
            fail_count++;
        }
    }

    // --- 3. 删除语义:rmdir 非空目录应失败 ---
    puts("\n[3] rmdir on non-empty dir should fail\n");
    expect_fail("rmdir /tmp/haha (non-empty)", rmdir("/tmp/haha"));
    expect_exists("/tmp/haha");   // 失败后目录还在

    // --- 4. 删除语义:unlink 目录应失败 ---
    puts("\n[4] unlink on a directory should fail\n");
    expect_fail("unlink /tmp/haha (is dir)", unlink("/tmp/haha"));
    expect_exists("/tmp/haha");

    // --- 5. unlink 文件成功 ---
    puts("\n[5] unlink a file\n");
    expect_ok("unlink /tmp/haha/file1", unlink("/tmp/haha/file1"));
    expect_absent("/tmp/haha/file1");

    // --- 6. rmdir 空目录成功 ---
    puts("\n[6] rmdir an empty dir\n");
    expect_ok("rmdir /tmp/haha/nihao (empty)", rmdir("/tmp/haha/nihao"));
    expect_absent("/tmp/haha/nihao");

    // --- 7. 递归删除整棵树 ---
    puts("\n[7] recursive remove\n");
    expect_ok("remove_recursive /tmp/haha", remove_recursive("/tmp/haha"));
    expect_absent("/tmp/haha");
    expect_absent("/tmp/haha/data");   // 子文件也应该没了

    // --- 8. 删不存在的东西应失败 ---
    puts("\n[8] removing nonexistent should fail\n");
    expect_fail("unlink /tmp/nonexist", unlink("/tmp/nonexist"));
    expect_fail("rmdir /tmp/nonexist", rmdir("/tmp/nonexist"));

    // --- 9. 在只读 ext4 上创建应失败 ---
    puts("\n[9] write on read-only ext4 should fail\n");
    expect_fail("mkdir /etc/foo (ext4 ro)", mkdir("/etc/foo"));

    // --- 总结 ---
    puts("\n=== summary ===\n");
    puts("PASS: "); put_int(pass_count); puts("\n");
    puts("FAIL: "); put_int(fail_count); puts("\n");
    if (fail_count == 0) {
        puts("ALL TESTS PASSED\n");
        return 0;
    } else {
        puts("SOME TESTS FAILED\n");
        return 1;
    }
}