/*
 * sys/socket.h —— 套接字接口
 *
 * 真正的定义在 net.h（socket/bind/listen/accept/connect/send/recv 都在那里），
 * 这里只补 AF_UNSPEC / AF_UNIX 和 shutdown 的 how。
 *
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
