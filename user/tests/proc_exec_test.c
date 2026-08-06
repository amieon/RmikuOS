#include "test.h"

/* 进程:exec 加载用户程序,验证替换执行 */
int main() {
    TEST_START("proc_exec");

    int pid = fork();
    if (pid < 0) {
        FAIL("fork 失败");
        TEST_END();
    }
    if (pid == 0) {
        /* 注意:exec 系统调用不做 PATH 查找,必须用完整路径(shell 才做 PATH 查找) */
        isize r = exec("/bin/hello");
        /* exec 成功不返回;到这里说明失败 */
        printf("exec failed: %d\n", (int)r);
        exit(1);
    }

    CHECK(pid > 0, "fork 成功");
    int code = -1;
    isize ret = waitpid(pid, &code, 0);
    CHECK(ret == pid, "waitpid 回收子进程");
    CHECK_EQ(code, 0, "exec(/bin/hello) 后子进程以 0 退出(exec 成功)");

    TEST_END();
}
