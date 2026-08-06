#include "test.h"

/* 文件系统基础:创建 / 写入 / 读回 / 重命名 / 删除(tmpfs) */
int main() {
    TEST_START("fs_crud");

    const char *p1 = "/tmp/test_crud.txt";
    const char *p2 = "/tmp/test_crud2.txt";
    const char *data = "hello rmiku fs";
    int len = (int)strlen(data);

    unlink(p1);
    unlink(p2);

    int fd = open(p1, O_CREAT | O_RDWR);
    CHECK(fd >= 0, "open(O_CREAT) 创建文件成功");

    isize n = write(fd, data, len);
    CHECK_EQ(n, len, "write 写入全部字节");
    CHECK(close(fd) == 0, "close 成功");

    fd = open(p1, O_RDONLY);
    CHECK(fd >= 0, "open(O_RDONLY) 重开文件成功");
    char buf[64];
    isize r = read(fd, buf, sizeof(buf) - 1);
    CHECK(r >= 0, "read 成功");
    buf[r] = 0;
    CHECK_EQ(r, len, "read 读回等长数据");
    CHECK_STREQ(buf, data, "读回内容与写入一致");
    close(fd);

    CHECK(rename(p1, p2) == 0, "rename 成功");
    struct stat st;
    CHECK(stat(p2, &st) == 0, "rename 后目标路径 stat 可见");
    CHECK(stat(p1, &st) < 0, "rename 后旧路径已不存在");

    CHECK(unlink(p2) == 0, "unlink 删除成功");
    CHECK(stat(p2, &st) < 0, "删除后文件不可见");

    TEST_END();
}
