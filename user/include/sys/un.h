/*
 * sys/un.h —— Unix 域套接字地址
 *
 * 现状：**只有结构体定义**，没有实现（没有 unix_pipe / named socket）。
 * 但 git-compat-util.h 在 POSIX 分支里无条件 include 它，且 connect.c 的
 * unix socket 分支会 sizeof(un.sun_path) 取缓冲区大小，所以结构体和
 * UNIX_PATH_MAX 必须存在。
 *
 * 只要不设 NO_UNIX_SOCKETS，这段代码就会参与编译 —— 结构体是"编译期契约"，
 * 真正 socket(AF_UNIX, ...) 时才会去内核要支持（当前返回失败，不影响本地 git）。
 *
 * 布局对齐 Linux：sun_family(2) + sun_path(108) = 110 字节。
 */
#ifndef RK_SYS_UN_H
#define RK_SYS_UN_H

#include "socket.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

struct sockaddr_un {
    sa_family_t sun_family;              /* AF_UNIX */
    char        sun_path[UNIX_PATH_MAX]; /* 以 NUL 结尾的路径，可能非 NUL 终止 */
};

#ifdef __cplusplus
}
#endif

#endif /* RK_SYS_UN_H */
