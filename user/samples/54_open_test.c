#include "user.h"

static void check(const char *label, isize got, int want_ok) {
    int got_ok = (got >= 0) ? 1 : 0;
    puts(label);
    puts(": ret=");
    put_int(got);
    if (got_ok == want_ok) {
        puts("  [PASS]\n");
    } else {
        puts("  [FAIL] <-- 预期");
        puts(want_ok ? "成功" : "失败");
        puts("\n");
    }
}

int main(int argc, char *argv[]) {
    const char *path = "/fat/permfile";
    char buf[64];

    // 准备:创建文件并写入初始内容(用 O_WRONLY|O_CREAT|O_TRUNC)
    int fd = open_create(path, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        puts("setup: cannot create test file\n");
        return 1;
    }
    write(fd, "hello", 5);
    close(fd);
    puts("=== setup done: wrote 'hello' to ");
    puts(path);
    puts(" ===\n\n");

    // ---- 测 O_RDONLY: read 应成功, write 应失败 ----
    puts("--- O_RDONLY (期望: read OK, write FAIL) ---\n");
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        puts("open O_RDONLY failed!\n");
    } else {
        check("  read ", read(fd, buf, sizeof(buf)), 1);   // 期望成功
        check("  write", write(fd, "X", 1), 0);            // 期望失败
        close(fd);
    }
    puts("\n");

    // ---- 测 O_WRONLY: read 应失败, write 应成功 ----
    puts("--- O_WRONLY (期望: read FAIL, write OK) ---\n");
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        puts("open O_WRONLY failed!\n");
    } else {
        check("  read ", read(fd, buf, sizeof(buf)), 0);   // 期望失败
        check("  write", write(fd, "Y", 1), 1);            // 期望成功
        close(fd);
    }
    puts("\n");

    // ---- 测 O_RDWR: read 和 write 都应成功 ----
    puts("--- O_RDWR (期望: read OK, write OK) ---\n");
    fd = open(path, O_RDWR);
    if (fd < 0) {
        puts("open O_RDWR failed!\n");
    } else {
        check("  read ", read(fd, buf, sizeof(buf)), 1);   // 期望成功
        check("  write", write(fd, "Z", 1), 1);            // 期望成功
        close(fd);
    }
    puts("\n");

    puts("=== permtest done ===\n");
    return 0;
}