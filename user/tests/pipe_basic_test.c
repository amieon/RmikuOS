#include "test.h"

/* 管道基础:同进程读写 + fork 跨进程读写 */
int main() {
    TEST_START("pipe_basic");

    int fd[2];
    CHECK(pipe(fd) == 0, "pipe 创建成功");
    CHECK(fd[0] >= 0 && fd[1] >= 0, "管道两端 fd 有效");

    /* 同进程:写后读 */
    write(fd[1], "hello", 5);
    char buf[16];
    isize n = read(fd[0], buf, sizeof(buf) - 1);
    CHECK(n >= 0, "read 成功");
    buf[n] = 0;
    CHECK_EQ(n, 5, "读回 5 字节");
    CHECK_STREQ(buf, "hello", "同进程管道内容一致");

    /* fork 跨进程:父写子读 */
    int pid = fork();
    if (pid < 0) {
        FAIL("fork 失败");
        TEST_END();
    }
    if (pid == 0) {
        char b2[16];
        isize r = read(fd[0], b2, sizeof(b2) - 1);
        b2[r] = 0;
        printf("child got: %s\n", b2);
        exit(0);
    }

    CHECK(pid > 0, "fork 成功");
    write(fd[1], "hi", 2);
    isize ret = waitpid(pid, 0, 0);
    CHECK(ret == pid, "waitpid 回收子进程");

    close(fd[0]);
    close(fd[1]);
    TEST_END();
}
