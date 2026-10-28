#include "test.h"

/* 会话/进程组 + /dev/null。
 *
 * 三件东西在这里一起测，因为它们是同一条依赖链上的：
 *   setsid() 要成功 → 进程不能是组长 → 必须先 fork
 *   daemonize() 的下一步就是 open("/dev/null") → 没有它 setsid 白做
 *   setsid 之后 pgid == pid，kill(-pgid) 才能只打到自己
 */

static void test_dev_null(void) {
    int fd = open("/dev/null", O_RDWR, 0);
    CHECK(fd >= 0, "/dev/null 可打开");
    if (fd < 0) return;

    /* 写多少就"成功"多少，但一个字节也没存下来 */
    char buf[16];
    for (int i = 0; i < 16; i++) buf[i] = 'x';
    CHECK_EQ(write(fd, buf, 16), 16, "/dev/null 写满额返回");

    /* 读永远 0（EOF），不是阻塞 */
    char rbuf[16];
    CHECK_EQ(read(fd, rbuf, 16), 0, "/dev/null 读立即 EOF");

    /* 必须是字符设备：git 的 sanitize_stdfds 之后有代码按 fstat 判断 */
    struct stat st;
    if (fstat(fd, &st) == 0) {
        CHECK(S_ISCHR(st.st_mode), "/dev/null 是字符设备");
    } else {
        FAIL("fstat(/dev/null) 失败");
    }

    close(fd);

    /* O_CREAT|O_EXCL 对已存在的设备照 POSIX 报 EEXIST，不能悄悄建个普通文件 */
    CHECK_LT(open("/dev/null", O_RDWR | O_CREAT | O_EXCL, 0644), 0,
             "/dev/null O_EXCL 失败");
}

static void test_setsid(void) {
    /* 测试进程是 shell setpgid 出来的组长（pgid == pid），所以自己调必 EPERM */
    CHECK_EQ(getpgrp(), getpid(), "测试进程自成一组");
    errno = 0;
    CHECK_EQ(setsid(), -1, "组长调用 setsid 失败");
    CHECK_EQ(errno, EPERM, "失败原因是 EPERM");

    /* 子进程继承父的 pgid（≠ 自己的 pid），所以它有资格新建会话 */
    int pid = fork();
    if (pid < 0) {
        FAIL("fork 失败");
        return;
    }
    if (pid == 0) {
        isize sid = setsid();
        if (sid != getpid()) exit(1);            /* 返回新 sid */
        if (getpgrp() != getpid()) exit(2);      /* 同时成为组长 */
        /* 再调一次：现在自己就是组长了 → EPERM */
        if (setsid() != -1 || errno != EPERM) exit(3);
        exit(0);
    }

    int code = -1;
    CHECK_EQ(waitpid(pid, &code, 0), pid, "waitpid 回收子进程");
    CHECK_EQ(code, 0, "子进程 setsid 语义全对");
}

int main() {
    TEST_START("proc_session");

    test_dev_null();
    test_setsid();

    TEST_END();
}
