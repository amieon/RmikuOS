#include "test.h"

/* 文件系统:lseek 定位(SEEK_SET / SEEK_CUR)后读回正确内容 */
int main() {
    TEST_START("fs_seek");

    const char *path = "/tmp/seek_test.txt";
    unlink(path);
    int fd = open(path, O_CREAT | O_RDWR);
    CHECK(fd >= 0, "open 创建成功");

    write(fd, "0123456789", 10);

    char buf[16];
    isize n = lseek(fd, 5, SEEK_SET);
    CHECK_EQ(n, 5, "lseek(SEEK_SET, 5) 定位成功");
    isize r = read(fd, buf, 5);
    CHECK(r >= 0, "read 成功");
    buf[r] = 0;
    CHECK_STREQ(buf, "56789", "从偏移 5 读回剩余内容");

    n = lseek(fd, -3, SEEK_CUR);
    CHECK_EQ(n, 7, "lseek(SEEK_CUR, -3) 相对定位成功");
    r = read(fd, buf, 3);
    buf[r] = 0;
    CHECK_STREQ(buf, "789", "SEEK_CUR 后读到 789");

    close(fd);
    unlink(path);
    TEST_END();
}
