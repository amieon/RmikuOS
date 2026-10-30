/*
 * misc_socket_fdset_test.c —— 只包含 <sys/socket.h> 也必须拿到 fd_set / select
 *
 * 为什么单独开一个文件：这条兼容性完全靠 sys/socket.h 里那句 `#include "select.h"`。
 * misc_posix_headers_test.c 自己显式 include 了 <sys/select.h>，而且排在
 * <sys/socket.h> 前面 —— 放在那里测，顺序会把问题挡住，等于没测。
 *
 * 现实依据（2026-10-31 OpenSSL bin 阶段）：
 *   include/internal/sockets.h 里 `#include <sys/select.h>` 只包在
 *   `#ifdef OPENSSL_SYS_AIX` 里（3.0 与 master 一致），但 apps/lib/apps.c 的
 *   wait_for_async() 直接用了 fd_set / FD_ZERO / openssl_fdset(=FD_SET) /
 *   select()。拿不到就炸成
 *     "unknown type name 'fd_set'; did you mean 'sk_set'?"（apps.c:3117）
 *   glibc 上 fd_set 经由一条复现不了的隐含链路到达，所以这里显式在 socket 层配齐。
 */
#include "test.h"

#include <unistd.h>        /* pipe / close / write */
#include <sys/socket.h>    /* 只要这一句 —— 故意不 include <sys/select.h> */

int main(void) {
    TEST_START("misc_socket_fdset");

    /* ---------- 1. 位图宽度自洽 ----------
     * fd_set 是定长位图，FD_SETSIZE 位必须正好塞进 sizeof(fd_set) 个字节。
     * 这两个数值分别由宏和数组长度算出来，改一个忘改另一个就会静默越界。 */
    CHECK_EQ((int)(sizeof(fd_set) * 8), FD_SETSIZE, "sizeof(fd_set) 正好装下 FD_SETSIZE 位");

    /* ---------- 2. FD_ZERO / FD_SET / FD_ISSET / FD_CLR ---------- */
    fd_set s;
    FD_ZERO(&s);
    CHECK_EQ(FD_ISSET(0, &s), 0, "FD_ZERO 之后全清");
    FD_SET(3, &s);
    CHECK(FD_ISSET(3, &s) != 0, "FD_SET(3) 之后 3 号位置位");
    CHECK_EQ(FD_ISSET(4, &s), 0, "只置了 3 号，4 号仍是 0");
    FD_CLR(3, &s);
    CHECK_EQ(FD_ISSET(3, &s), 0, "FD_CLR 能清掉");
    CHECK_EQ(FD_ISSET(3, &s), 0, "重复 FD_CLR 不出错");

    /* ---------- 3. 边界 fd 与越界写保护 ---------- */
    FD_ZERO(&s);
    FD_SET(FD_SETSIZE - 1, &s);
    CHECK(FD_ISSET(FD_SETSIZE - 1, &s) != 0, "最高位 fd 能置位");
    FD_SET(FD_SETSIZE, &s);       /* 越界：必须静默忽略，不能写穿位图 */
    CHECK(FD_ISSET(FD_SETSIZE - 1, &s) != 0, "越界 FD_SET 不影响已置的位");
    CHECK_EQ(FD_ISSET(FD_SETSIZE, &s), 0, "越界 fd 查询恒为 0");

    /* ---------- 4. 真调一次 select（走 poll 转换） ---------- */
    int p[2];
    CHECK_EQ(pipe(p), 0, "pipe 建好");

    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(p[0], &rd);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    /* 空管道 + 0 超时 → 0，且 rd 里不该残留就绪位 */
    int n = select(p[0] + 1, &rd, (fd_set *)0, (fd_set *)0, &tv);
    CHECK_EQ(n, 0, "没数据时 select 返回 0");
    CHECK_EQ(FD_ISSET(p[0], &rd), 0, "返回 0 时 fd_set 里不该有就绪位");

    /* 写一个字节后必须变成可读 */
    CHECK_EQ((int)write(p[1], "x", 1), 1, "往管道写 1 字节");
    FD_ZERO(&rd);
    FD_SET(p[0], &rd);
    n = select(p[0] + 1, &rd, (fd_set *)0, (fd_set *)0, &tv);
    CHECK_EQ(n, 1, "有数据时 select 返回 1");
    CHECK(FD_ISSET(p[0], &rd) != 0, "就绪位压回 rd 里");

    close(p[0]);
    close(p[1]);

    TEST_END();
}
