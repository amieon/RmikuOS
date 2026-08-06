#include "test.h"

/* 权限边界:凭证 fork 继承 + 降权后无法提权 */
int main() {
    TEST_START("syscall_perm");

    /* 父进程先降权,再 fork:子进程必须继承降权后的凭证,且无法提权 */
    CHECK(setuid(100) == 0, "父进程 setuid(100) 降权成功");

    int pid = fork();
    if (pid < 0) {
        FAIL("fork 失败");
        TEST_END();
    }
    if (pid == 0) {
        if (getuid() != 100 || geteuid() != 100) exit(1);  /* 继承失败 */
        if (setuid(0) == 0) exit(2);                       /* 提权成功(不允许) */
        exit(0);
    }
    int code = -1;
    waitpid(pid, &code, 0);
    CHECK_EQ(code, 0, "子进程继承降权凭证且无法提权");

    /* 父进程自己(已降权)也不能提权 */
    CHECK(setuid(0) != 0, "已降权父进程 setuid(0) 被拒绝");

    TEST_END();
}
