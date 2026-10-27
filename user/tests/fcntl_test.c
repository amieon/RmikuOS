/* fcntl 语义测试
 *
 * 这里钉的不是"命令支不支持"，而是三个**不崩但语义错**的坑：
 *
 *   1. `F_GETFL` 必须报出 open 那一刻的访问模式。以前 fd_flags 从没被
 *      open 填过，`fcntl(fd, F_GETFL) & O_ACCMODE` 恒为 O_RDONLY ——
 *      用 O_RDWR 打开的文件看起来只读，反之亦然。
 *   2. `F_SETFL` **只能**改 O_APPEND / O_NONBLOCK。整份覆盖会把访问模式
 *      一起冲掉：`fcntl(fd, F_SETFL, O_NONBLOCK)` 之后 write 就 EBADF 了。
 *   3. `F_DUPFD` 的新 fd 不带 FD_CLOEXEC，`F_DUPFD_CLOEXEC` 才带；
 *      两者都要落到「≥ 第三个参数的最小空闲槽」。
 */
#include "user.h"

int main(void) {
    TEST_START("fcntl");

    int fd = open("/tmp/fcntl_a", O_CREAT | O_RDWR, 0666);
    CHECK(fd >= 0, "open(O_CREAT|O_RDWR) 成功");
    if (fd < 0) TEST_END();

    /* ---- 1. F_GETFL 报访问模式 ---- */
    int fl = fcntl(fd, F_GETFL);
    CHECK(fl >= 0, "F_GETFL 成功");
    CHECK_EQ(fl & O_ACCMODE, O_RDWR, "O_RDWR 打开 -> F_GETFL 报 O_RDWR");

    /* dup 出来的是同一个 open file description，访问模式必须跟着走。
     * （我们把标志按 fd 存，不搬一份的话这里会退回 O_RDONLY。） */
    int dfd = dup(fd);
    CHECK(dfd >= 0, "dup 成功");
    if (dfd >= 0) {
        CHECK_EQ(fcntl(dfd, F_GETFL) & O_ACCMODE, O_RDWR,
                 "dup 出来的 fd 保留 O_RDWR（同一 open file description）");
        close(dfd);
    }

    close(fd);

    fd = open("/tmp/fcntl_a", O_RDONLY);
    CHECK(fd >= 0, "以 O_RDONLY 重新打开成功");
    if (fd >= 0) {
        fl = fcntl(fd, F_GETFL);
        CHECK_EQ(fl & O_ACCMODE, O_RDONLY, "O_RDONLY 打开 -> F_GETFL 报 O_RDONLY");

        /* ---- 2. F_SETFL 只改状态标志 ---- */
        CHECK_EQ(fcntl(fd, F_SETFL, O_NONBLOCK), 0, "F_SETFL(O_NONBLOCK) 成功");
        fl = fcntl(fd, F_GETFL);
        CHECK((fl & O_NONBLOCK) != 0, "O_NONBLOCK 置上了");
        CHECK_EQ(fl & O_ACCMODE, O_RDONLY,
                 "置 O_NONBLOCK 不会把访问模式冲掉");

        CHECK_EQ(fcntl(fd, F_SETFL, 0), 0, "F_SETFL(0) 成功");
        fl = fcntl(fd, F_GETFL);
        CHECK((fl & O_NONBLOCK) == 0, "O_NONBLOCK 清掉了");
        CHECK_EQ(fl & O_ACCMODE, O_RDONLY, "清 O_NONBLOCK 后访问模式仍是 O_RDONLY");

        /* ---- 3. F_GETFD / F_SETFD ---- */
        CHECK_EQ(fcntl(fd, F_GETFD), 0, "新开的 fd 没有 FD_CLOEXEC");
        CHECK_EQ(fcntl(fd, F_SETFD, FD_CLOEXEC), 0, "F_SETFD(FD_CLOEXEC) 成功");
        CHECK_EQ(fcntl(fd, F_GETFD) & FD_CLOEXEC, FD_CLOEXEC, "FD_CLOEXEC 置上了");
        CHECK_EQ(fcntl(fd, F_SETFD, 0), 0, "F_SETFD(0) 成功");
        CHECK_EQ(fcntl(fd, F_GETFD), 0, "FD_CLOEXEC 清掉了");

        /* ---- 4. F_DUPFD：落到 >= min 的空闲槽，且不带 cloexec ---- */
        int nfd = fcntl(fd, F_DUPFD, 100);
        CHECK(nfd >= 100, "F_DUPFD(100) 拿到 >= 100 的 fd");
        if (nfd >= 0) {
            CHECK(nfd != fd, "F_DUPFD 给的是新 fd");
            CHECK_EQ(fcntl(nfd, F_GETFL) & O_ACCMODE, O_RDONLY,
                     "复制出来的 fd 共享同一个打开描述（访问模式一致）");
            CHECK_EQ(fcntl(nfd, F_GETFD), 0, "F_DUPFD 得到的新 fd 不带 FD_CLOEXEC");
            close(nfd);
        }

        int cfd = fcntl(fd, F_DUPFD_CLOEXEC, 200);
        CHECK(cfd >= 200, "F_DUPFD_CLOEXEC(200) 拿到 >= 200 的 fd");
        if (cfd >= 0) {
            CHECK(cfd != fd, "F_DUPFD_CLOEXEC 给的是新 fd");
            CHECK_EQ(fcntl(cfd, F_GETFD) & FD_CLOEXEC, FD_CLOEXEC,
                     "F_DUPFD_CLOEXEC 的新 fd 带 FD_CLOEXEC");
            close(cfd);
        }

        /* ---- 5. dup3 接收 O_CLOEXEC ---- */
        CHECK_EQ(dup3(fd, 10, O_CLOEXEC), 10, "dup3(fd, 10, O_CLOEXEC) 成功");
        CHECK_EQ(fcntl(10, F_GETFD) & FD_CLOEXEC, FD_CLOEXEC, "dup3 的 O_CLOEXEC 生效了");
        close(10);

        /* ---- 6. 未知命令必须明确失败，不能像"成功"一样返回 0 ---- */
        errno = 0;
        CHECK_EQ(fcntl(fd, 0x7fff, 0), -1, "未知 fcntl 命令返回 -1");
        CHECK_EQ(errno, EINVAL, "未知 fcntl 命令置 EINVAL（不是笼统的 EIO）");

        close(fd);
    }

    TEST_END();
}
