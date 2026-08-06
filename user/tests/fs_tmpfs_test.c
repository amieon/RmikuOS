#include "test.h"

/* 文件系统:目录创建/嵌套/删除(tmpfs) */
int main() {
    TEST_START("fs_tmpfs");

    const char *base = "/tmp/t_dir";
    const char *sub = "/tmp/t_dir/sub";
    const char *file = "/tmp/t_dir/sub/f.txt";

    /* 清场(容忍不存在) */
    unlink(file);
    rmdir(sub);
    rmdir(base);

    CHECK(mkdir(base, 0) == 0, "mkdir 顶层目录成功");
    CHECK(mkdir(sub, 0) == 0, "mkdir 嵌套子目录成功");

    int fd = open(file, O_CREAT | O_RDWR);
    CHECK(fd >= 0, "在子目录中创建文件成功");
    write(fd, "x", 1);
    close(fd);

    struct stat st;
    CHECK(stat(file, &st) == 0, "嵌套文件 stat 可见");
    CHECK(stat(sub, &st) == 0, "子目录 stat 可见");

    CHECK(unlink(file) == 0, "unlink 嵌套文件成功");
    CHECK(rmdir(sub) == 0, "rmdir 子目录成功");
    CHECK(rmdir(base) == 0, "rmdir 父目录成功");
    CHECK(stat(base, &st) < 0, "目录删除后不可见");

    TEST_END();
}
