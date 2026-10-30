/*
 * sys/socket.h —— 套接字接口
 *
 * 真正的定义在 net.h（socket/bind/listen/accept/connect/send/recv 都在那里），
 * 这里只补 AF_UNSPEC / AF_UNIX 和 shutdown 的 how。
 *
 * 注意 struct sockaddr / sa_family_t **也在 net.h**：bind/connect 现在按
 * sa_family 分派，必须能看见这个结构体；写在 socket.h 会变成
 * "socket.h 包含 net.h，net.h 又要 socket.h" 的循环包含。
 *
 * 单向依赖：sys/socket.h -> net.h。反过来绝不允许（循环包含的症状是
 * "结构体忽然不完整"，很难查）。
 */
#ifndef RK_SYS_SOCKET_H
#define RK_SYS_SOCKET_H

#include "../net.h"

/*
 * 为什么 socket 层要顺手带上 <sys/select.h>
 *
 * OpenSSL 的 include/internal/sockets.h 里，`#include <sys/select.h>` 只包在
 * `#ifdef OPENSSL_SYS_AIX` 里（3.0 到 master 都是这样），可是 apps/lib/apps.c
 * 的 wait_for_async() 直接用了 fd_set / FD_ZERO / openssl_fdset(=FD_SET) /
 * select()。在 glibc 上它能编过，是因为 fd_set 经由我们复现不了的那条隐含链路
 * 到达；在只认头文件字面约定的实现上就会炸成
 *   "unknown type name 'fd_set'; did you mean 'sk_set'?"
 * （2026-10-31 bin 阶段就撞在 apps.c:3117 上）。
 *
 * "选 socket 的 fd 集合"本来就是 socket 的配套动作，而 sockets.h 无论
 * OPENSSL_SYS_UNIX 有没有定义都会包含 <sys/socket.h>，所以挂在这里是唯一
 * 两条路都能走通的位置。glibc 没有这条包含，所以这是**有意的兼容性放宽**，
 * 不是照抄：代价是 socket 用户多拿一个 256 位的 fd_set 类型和一个内联 select()。
 */
#include "select.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif
#ifndef AF_UNIX
#define AF_UNIX   1
#endif
#ifndef AF_INET6
#define AF_INET6  10
#endif

#ifndef RK_SOCKADDR_STORAGE
#define RK_SOCKADDR_STORAGE
struct sockaddr_storage {
    sa_family_t ss_family;
    char        __ss_pad[128 - sizeof(sa_family_t)];
};
#endif

/* shutdown() 的 how */
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

/* recv/send 的 flags（MSG_PEEK、MSG_DONTWAIT 等）定义在 net.h，
 * 和 recv/recvfrom 的声明放在一起 —— 只包含 net.h 的代码也必须能拿到。 */

/*
 * shutdown(fd, how)：套接字半关闭（SHUT_WR 发 FIN，对端能读到 EOF）。
 *
 * 名字只和"关机"撞，但那个已经改叫 rk_shutdown() 了（见 arch.h），所以这里
 * 可以老实叫 shutdown —— POSIX 有的名字不该加前缀，否则可移植代码还得改。
 * 对非套接字 fd 内核返回 -1（POSIX 是 ENOTSOCK）。
 */
static inline int shutdown(int fd, int how) {
    isize r = rk_sys_ret(syscall3(SYS_NET_SHUTDOWN, (usize)fd, (usize)how, 0));
    return (r < 0) ? -1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RK_SYS_SOCKET_H */
