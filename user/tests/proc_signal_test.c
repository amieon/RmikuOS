#include "test.h"

/* 信号:子进程忽略 SIGTERM,父进程发信号,验证不被杀死 */
int main() {
    TEST_START("proc_signal");

    int pid = fork();
    if (pid < 0) {
        FAIL("fork 失败");
        TEST_END();
    }
    if (pid == 0) {
        /* 子进程:忽略 SIGTERM,然后睡 20 ticks 再正常退出 */
        signal(SIGTERM, SIG_IGN);
        sleep(20);
        puts("[child] alive after SIGTERM\n");
        exit(0);
    }

    CHECK(pid > 0, "fork 成功");
    sleep(5);               /* 等子进程装好 handler */
    kill(pid, SIGTERM);     /* 发信号:应被忽略 */
    int code = -1;
    isize ret = waitpid(pid, &code, 0);
    CHECK(ret == pid, "waitpid 回收子进程");
    CHECK_EQ(code, 0, "SIGTERM 被忽略,子进程正常退出");

    TEST_END();
}
