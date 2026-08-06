#include "test.h"

/* 系统调用:stat 时间戳(mtime 非零,NTP 校准后为真 epoch) */
int main() {
    TEST_START("syscall_stat_time");

    const char *path = "/tmp/time_test.txt";
    unlink(path);

    int fd = open(path, O_CREAT | O_RDWR);
    CHECK(fd >= 0, "open 创建成功");
    write(fd, "t", 1);
    close(fd);

    struct stat st;
    CHECK(stat(path, &st) == 0, "stat 成功");
    CHECK(st.st_size == 1, "st_size 正确");
    CHECK(st.st_mtime > 0, "st_mtime 非零(有墙钟或 tick 时间)");

    unlink(path);
    TEST_END();
}
