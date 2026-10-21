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

#ifdef __cplusplus
}
#endif

#endif /* RK_SYS_SOCKET_H */
