/*
 * setsockopt 的宿主回归（被测代码从 user/include/net.h 抽出）。
 *
 * 只为一个症状服务：OpenSSL 的 BIO_connect 无条件设 IPPROTO_TCP/TCP_NODELAY，
 * 失败就报 "unable to nodelay" 并**拒绝连接**：
 *     error: ... BIO_connect: ... calling setsockopt()
 *     connect:errno=92            (92 = ENOPROTOOPT)
 * 于是 `openssl s_client` 连不上任何站点，curl 却只是忽略这个失败而不报错 ——
 * 一个被掩盖很久的内核缺口。
 *
 * 这里锁住三件事：
 *   1. TCP_NODELAY=1 返回 0（我们的 TCP 本来就没有 Nagle，这是既有行为）
 *   2. TCP_NODELAY=0 返回 -1/ENOPROTOOPT（要求开 Nagle = 我们做不到）
 *   3. 这条分支**不许**落到 syscall（设置 NODELAY 不需要惊动内核）
 */
#include <stdio.h>
#include <errno.h>
#include <string.h>

typedef unsigned int socklen_t;

#define SOL_SOCKET   1
#define SO_REUSEADDR 2
#define IPPROTO_TCP  6
#define TCP_NODELAY  1
#define TCP_MAXSEG   2

/* 桩：真实实现走 SYS_NET_SETSOCKOPT，宿主上没有那个 syscall。 */
static int g_calls;
static int g_fd, g_opt, g_val;

static int net_setsockopt(int fd, int optname, int optval) {
    g_calls++;
    g_fd = fd;
    g_opt = optname;
    g_val = optval;
    return 0;
}

#include "_sockopt.inc"

static int total, fails;

static void ok(int cond, const char *what) {
    total++;
    if (!cond) {
        fails++;
        printf("FAIL  %s\n", what);
    }
}

int main(void) {
    int on = 1, off = 0;

    g_calls = 0;
    errno = 0;
    ok(setsockopt(7, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on) == 0,
       "TCP_NODELAY=1 应返回 0");
    ok(g_calls == 0, "TCP_NODELAY 不应落到 syscall");
    ok(errno == 0, "TCP_NODELAY=1 不应动 errno");

    errno = 0;
    ok(setsockopt(7, IPPROTO_TCP, TCP_NODELAY, &off, sizeof off) == -1,
       "TCP_NODELAY=0 应失败（我们没有 Nagle 可开）");
    ok(errno == ENOPROTOOPT, "TCP_NODELAY=0 应报 ENOPROTOOPT");

    errno = 0;
    ok(setsockopt(7, IPPROTO_TCP, TCP_MAXSEG, &on, sizeof on) == -1,
       "TCP_MAXSEG 应诚实拒绝");
    ok(errno == ENOPROTOOPT, "TCP_MAXSEG 应报 ENOPROTOOPT");

    /* 值缺失（NULL 或 optlen 太短）→ on 按 0 处理 → 走"拒绝"那一侧，
     * 不能因为"没给值"就当成 1 放过去。 */
    errno = 0;
    ok(setsockopt(7, IPPROTO_TCP, TCP_NODELAY, NULL, sizeof on) == -1,
       "optval=NULL 应按 0 处理并拒绝");
    errno = 0;
    ok(setsockopt(7, IPPROTO_TCP, TCP_NODELAY, &on, 1) == -1,
       "optlen<sizeof(int) 应按 0 处理并拒绝");

    /* SOL_SOCKET 那条老路径没被改坏：仍然转 syscall，参数原样。 */
    g_calls = 0; g_fd = g_opt = g_val = -1;
    errno = 0;
    ok(setsockopt(9, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) == 0,
       "SO_REUSEADDR 应成功");
    ok(g_calls == 1 && g_fd == 9 && g_opt == SO_REUSEADDR && g_val == 1,
       "SO_REUSEADDR 应原样转给内核（fd/optname/value）");

    errno = 0;
    ok(setsockopt(9, SOL_SOCKET, 99, &on, sizeof on) == -1 && errno == ENOPROTOOPT,
       "未知 SOL_SOCKET 选项应报 ENOPROTOOPT");

    printf("[sockopt] %d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
