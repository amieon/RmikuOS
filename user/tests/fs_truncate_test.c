#include "test.h"

/* 文件系统:ftruncate(fd) / truncate(path) 缩小与扩大 */
static isize file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) return -1;
    return (isize)st.st_size;
}

int main() {
    TEST_START("fs_truncate");

    const char *path = "/tmp/trunc_test.txt";
    unlink(path);

    int fd = open(path, O_CREAT | O_RDWR);
    CHECK(fd >= 0, "open 创建成功");
    CHECK_EQ(write(fd, "abcdefghijklmnopqrstuvwxyz", 26), 26, "写入 26 字节");
    close(fd);

    /* ftruncate 缩小:10 字节,前 10 字节内容保留 */
    fd = open(path, O_RDWR);
    CHECK(ftruncate(fd, 10) == 0, "ftruncate 缩小到 10 字节成功");
    CHECK_EQ(file_size(path), 10, "文件大小变为 10");
    char buf[16];
    isize n = read(fd, buf, 10);
    buf[n] = 0;
    CHECK_STREQ(buf, "abcdefghij", "缩小后前 10 字节内容保留");
    close(fd);

    /* ftruncate 扩大:补零 */
    fd = open(path, O_RDWR);
    CHECK(ftruncate(fd, 20) == 0, "ftruncate 扩大到 20 字节成功");
    close(fd);
    CHECK_EQ(file_size(path), 20, "文件大小变为 20");

    /* truncate(path) 路径版 */
    CHECK(truncate(path, 5) == 0, "truncate(path) 缩小到 5 字节成功");
    CHECK_EQ(file_size(path), 5, "文件大小变为 5");
    CHECK(truncate("/tmp/nonexist_xx", 5) < 0, "truncate 不存在的路径失败");

    /* 坏 fd */
    CHECK(ftruncate(999, 10) < 0, "ftruncate 坏 fd 失败");

    unlink(path);
    TEST_END();
}
