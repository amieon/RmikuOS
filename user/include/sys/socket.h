/*
 * sys/socket.h —— 套接字接口
 *
 * 真正的定义在 net.h（socket/bind/listen/accept/connect/send/recv 都在那里），
 * 这里补上 POSIX 要求而 net.h 没写的部分：通用 sockaddr、AF_UNSPEC、shutdown。
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

/* 地址族类型。POSIX 要求它存在，且 struct sockaddr 的第一个字段就是它。
 * 这里用 unsigned short（16 位），与内核回传的 family 宽度一致。 */
#ifndef RK_SA_FAMILY_T
#define RK_SA_FAMILY_T
typedef unsigned short sa_family_t;
#endif

/* 通用地址头：具体协议的结构都以它为前缀（POSIX 的要求） */
struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif
#ifndef AF_UNIX
#define AF_UNIX   1
#endif

/* shutdown() 的 how */
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#ifdef __cplusplus
}
#endif

#endif /* RK_SYS_SOCKET_H */
