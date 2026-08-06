#include "test.h"

/* 文件系统:fsync 正常成功 / 坏 fd 失败 */
int main() {
    TEST_START("fs_fsync");

    const char *path = "/tmp/fsync_test.txt";
    unlink(path);

    int fd = open(path, O_CREAT | O_RDWR);
    CHECK(fd >= 0, "open 创建成功");
    write(fd, "data", 4);
    CHECK(fsync(fd) == 0, "fsync 正常文件成功");
    close(fd);

    CHECK(fsync(999) < 0, "fsync 坏 fd 失败");

    unlink(path);
    TEST_END();
}
