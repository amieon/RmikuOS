#include "user.h"

// 简单的字符串拼接: dir + "/" + name -> out
static void make_path(const char *dir, const char *name, char *out) {
    int p = 0;
    for (int i = 0; dir[i]; i++) out[p++] = dir[i];
    if (p > 0 && out[p-1] != '/') out[p++] = '/';
    for (int i = 0; name[i]; i++) out[p++] = name[i];
    out[p] = 0;
}

// 把文件全部内容读出来打印(用 O_RDONLY)
static void dump(const char *path) {
    char buf[128];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        puts("  (cannot open ");
        puts(path);
        puts(")\n");
        return;
    }
    puts("  content=[");
    while (1) {
        isize n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        write(1, buf, n);   // 原样输出读到的字节
    }
    puts("]\n");
    close(fd);
}

// 取文件大小
static isize file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) return -1;
    return (isize)st.size;
}

static void test_truncate(const char *dir) {
    char path[128];
    make_path(dir, "trunc_test", path);

    puts("=== TRUNCATE test (>) ===\n");

    // 写一个长内容
    int fd = open_create(path, O_WRONLY | O_TRUNC);
    write(fd, "AAAAAAAAAA", 10);   // 10 字节
    close(fd);
    puts("after write 10 bytes:\n");
    puts("  size=");
    put_int(file_size(path));
    puts("\n");
    dump(path);

    // 再用 O_TRUNC 打开,写短内容 -> 应该截断,不残留
    fd = open_create(path, O_WRONLY | O_TRUNC);
    write(fd, "BB", 2);            // 2 字节
    close(fd);
    puts("after truncate-rewrite 'BB':\n");
    puts("  size=");
    put_int(file_size(path));
    puts("  (期望 2)\n");
    dump(path);                    // 期望 content=[BB],不能有 A 残留

    puts("\n");
}

static void test_append(const char *dir) {
    char path[128];
    make_path(dir, "append_test", path);

    puts("=== APPEND test (>>) ===\n");

    // 先覆盖建立初始内容
    int fd = open_create(path, O_WRONLY | O_TRUNC);
    write(fd, "aaa", 3);
    close(fd);
    puts("after init 'aaa':\n");
    puts("  size=");
    put_int(file_size(path));
    puts("\n");
    dump(path);

    // O_APPEND 打开,写 -> 应追加到末尾
    fd = open_create(path, O_WRONLY | O_APPEND);
    write(fd, "bbb", 3);
    close(fd);
    puts("after append 'bbb':\n");
    puts("  size=");
    put_int(file_size(path));
    puts("  (期望 6)\n");
    dump(path);                    // 期望 content=[aaabbb]

    // 再 append 一次,确认每次都到末尾
    fd = open_create(path, O_WRONLY | O_APPEND);
    write(fd, "ccc", 3);
    close(fd);
    puts("after append 'ccc':\n");
    puts("  size=");
    put_int(file_size(path));
    puts("  (期望 9)\n");
    dump(path);                    // 期望 content=[aaabbbccc]

    puts("\n");
}

int main(int argc, char *argv[]) {
    // 默认测 /tmp,命令行参数可指定别的目录(盘)
    const char *dir = "/tmp";
    if (argc >= 2) {
        dir = argv[1];
    }

    puts("######## testing in dir: ");
    puts(dir);
    puts(" ########\n\n");

    test_truncate(dir);
    test_append(dir);

    puts("######## done ########\n");
    return 0;
}