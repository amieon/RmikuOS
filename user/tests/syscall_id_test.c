#include "test.h"

/* 系统调用:进程凭证查询与特权切换(uid/euid/gid/egid) */
int main() {
    TEST_START("syscall_id");

    CHECK_EQ(getuid(), 0, "初始 uid=0(root)");
    CHECK_EQ(geteuid(), 0, "初始 euid=0");
    CHECK_EQ(getgid(), 0, "初始 gid=0");
    CHECK_EQ(getegid(), 0, "初始 egid=0");

    /* seteuid 可在特权与普通间来回(不改变 uid) */
    CHECK(seteuid(100) == 0, "特权下 seteuid(100) 成功");
    CHECK_EQ(geteuid(), 100, "euid 变为 100");
    CHECK(seteuid(0) == 0, "特权下 seteuid(0) 恢复成功");
    CHECK_EQ(geteuid(), 0, "euid 恢复为 0");

    /* setuid 一次性降权:在子进程里做,避免影响本进程后续断言 */
    int pid = fork();
    if (pid < 0) {
        FAIL("fork 失败");
        TEST_END();
    }
    if (pid == 0) {
        if (setuid(100) != 0) exit(1);
        if (getuid() != 100 || geteuid() != 100) exit(2);
        if (setuid(0) == 0) exit(3);   /* 非特权提权必须失败 */
        exit(0);
    }
    int code = -1;
    waitpid(pid, &code, 0);
    CHECK_EQ(code, 0, "setuid 降权 + 禁止提权(子进程)");

    TEST_END();
}
