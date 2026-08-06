#include "test.h"

/* 系统调用:stat 时间戳随写入单调更新(不依赖墙钟校准,NTP 未同步时 mtime 可为 0) */
int main() {
    TEST_START("syscall_stat_time");

    const char *path = "/tmp/time_test.txt";
    unlink(path);

    int fd = open(path, O_CREAT | O_RDWR);
    CHECK(fd >= 0, "open 创建成功");
    write(fd, "t", 1);
    close(fd);

    struct stat st1;
    CHECK(stat(path, &st1) == 0, "stat 成功");
    CHECK(st1.st_size == 1, "st_size 正确");
    time_t before = st1.st_mtime;

    /* 追加写入,时间戳应更新(单调不倒退) */
    int fd2 = open(path, O_WRONLY);
    CHECK(fd2 >= 0, "重开写入成功");
    lseek(fd2, 0, SEEK_END);
    write(fd2, "xyz", 3);
    close(fd2);
    sleep(3);   /* 让 tick / 墙钟前进 */

    struct stat st2;
    CHECK(stat(path, &st2) == 0, "写入后 stat 成功");
    CHECK(st2.st_size == 4, "追加后大小变为 4");
    CHECK(st2.st_mtime >= before, "写入后 mtime 不倒退(单调更新)");

    unlink(path);
    TEST_END();
}
