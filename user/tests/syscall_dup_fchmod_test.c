/* dup / dup3 / fchmod / fdatasync 测试
 *
 * 覆盖：
 *   1. dup：返回最低空闲 fd；新旧 fd **共享文件偏移**（这是 dup 与 open 的本质区别）
 *   2. dup 复用被 close 掉的低位 fd
 *   3. dup3：flags=0 正常；oldfd==newfd 返回 -1（POSIX 规定，与 dup2 的 no-op 不同）；
 *            非零 flags 明确拒绝（O_CLOEXEC 还没实现，不假装支持）
 *   4. fchmod：按 fd 改权限，之后 stat 能读到新 mode；
 *             对只读 rootfs 的 fd（没有可定位 inode）返回 -1
 *   5. fdatasync：alias 到 fsync，调用成功即可
 */
#include "user.h"

static int fails = 0;

static void check(int cond, const char *msg) {
    if (cond) {
        puts("  OK   ");
    } else {
        puts("  FAIL ");
        fails++;
    }
    puts(msg);
    rk_put_char('\n');
}

int main() {
    const char *path = "/tmp/du.bin";
    char buf[16];

    puts("=== dup / dup3 / fchmod / fdatasync 测试 ===\n");

    /* ---------- 1. dup 与共享偏移 ---------- */
    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC);
    check(fd >= 0, "open(O_CREAT|O_RDWR|O_TRUNC)");
    check(write(fd, "abc", 3) == 3, "write 3 字节(偏移到 3)");

    int d1 = dup(fd);
    check(d1 > fd, "dup 得到比原 fd 大的新 fd(最低空闲)");
    check(d1 != fd, "新 fd 与原 fd 不同");

    /* 通过原 fd 把偏移拨回 0，新 fd 应能读到同样内容 —— 证明共享偏移 */
    check(lseek(fd, 0, SEEK_SET) == 0, "lseek(fd, 0, SEEK_SET)");
    isize n = read(d1, buf, 3);
    buf[n > 0 ? n : 0] = 0;
    check(n == 3 && buf[0] == 'a' && buf[2] == 'c', "通过 dup 的 fd 读到 \"abc\" -> 偏移共享");
    check(lseek(d1, 0, SEEK_CUR) == 3, "读完后偏移是 3(两个 fd 看到同一个偏移)");

    close(d1);

    /* ---------- 2. dup 复用低位空闲 fd ---------- */
    int d2 = dup(fd);
    check(d2 == d1, "close 后 dup 又拿到同一个低位 fd");
    close(d2);

    /* ---------- 3. dup3 ---------- */
    int d3 = dup3(fd, 10, 0);
    check(d3 == 10, "dup3(fd, 10, 0) -> 10");
    if (d3 >= 0) close(d3);

    check(dup3(fd, fd, 0) == -1, "dup3(fd, fd, 0) -> -1(与 dup2 的 no-op 不同)");
    check(dup3(fd, 11, 1) == -1, "dup3(fd, 11, flags=1) -> -1(flags 暂不支持，明确拒绝)");

    /* ---------- 3b. 表增长后中间空槽必须可复用 ----------
     * dup3(fd,10) 会把 fd 表从 ~5 拉到 11，中间 5..9 是"关闭"状态。
     * 它们必须登记进 free_fds 才能被后续 open 拿到；否则每做一次高位 dup，
     * 就是一次永久 fd 泄漏（表一路往上涨，直到 MAX_FD 才失败）。
     * 判据：连续 6 次 open 得到的 fd 都不超过 10。 */
    int grow_fds[6];
    int grow_ok = 1;
    for (int i = 0; i < 6; ++i) {
        grow_fds[i] = open(path, O_RDONLY);
        if (grow_fds[i] < 0 || grow_fds[i] > 10) grow_ok = 0;
    }
    check(grow_ok, "增长表后中间空槽被 open 复用(无 fd 泄漏)");
    for (int i = 0; i < 6; ++i) {
        if (grow_fds[i] >= 0) close(grow_fds[i]);
    }


    /* ---------- 4. fchmod ---------- */
    check(fchmod(fd, 0600) == 0, "fchmod(fd, 0600)");
    struct stat st;
    check(stat(path, &st) == 0, "stat 取属性");
    check((st.st_mode & 0777) == 0600, "stat 读到 mode == 0600(fchmod 生效)");

    check(fchmod(fd, 0640) == 0, "fchmod(fd, 0640)");
    check(stat(path, &st) == 0 && (st.st_mode & 0777) == 0640, "再次 stat -> 0640");

    /* 只读 rootfs 上的 fd：ReadOnlyMemFile 没有可定位 inode -> -1 */
    int rfd = open("/etc/passwd", O_RDONLY);
    if (rfd >= 0) {
        check(fchmod(rfd, 0777) == -1, "fchmod 只读 rootfs 的 fd -> -1(无可定位 inode)");
        close(rfd);
    } else {
        puts("  SKIP  /etc/passwd 打不开，跳过只读 fd 用例\n");
    }

    /* ---------- 5. fdatasync ---------- */
    check(fdatasync(fd) == 0, "fdatasync(fd) == 0(alias 到 fsync)");

    close(fd);

    if (fails == 0) {
        puts("\n全部通过\n");
        return 0;
    }
    printf("\n%d 项失败\n", fails);
    return 1;
}
