#include "test.h"

/* 进程基础:fork / waitpid / 退出码传递 */
int main() {
    TEST_START("proc_fork");

    int pid = fork();
    if (pid < 0) {
        FAIL("fork 失败");
        TEST_END();
    }
    if (pid == 0) {
        /* 子进程:睡一会后以 42 退出(不打任何断言输出) */
        sleep(2);
        exit(42);
    }

    CHECK(pid > 0, "fork 成功");
    int code = -1;
    isize ret = waitpid(pid, &code, 0);
    CHECK(ret == pid, "waitpid 返回子进程 pid");
    CHECK_EQ(code, 42, "子进程退出码正确传递");

    TEST_END();
}
