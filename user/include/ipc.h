#pragma once
#ifdef __cplusplus
extern "C" {
#endif



#include "syscall.h"

static inline int pipe(int fd[2]) {
    return syscall3(SYS_PIPE, (usize)fd, 0, 0);
}

static inline int dup2(int old_fd, int new_fd) {
    return syscall3(SYS_DUP2, (usize)old_fd, (usize)new_fd, 0);
}

/* dup(fd): 复制到最低空闲 fd，返回新 fd（目标由内核挑）。 */
static inline int dup(int old_fd) {
    return syscall3(SYS_DUP, (usize)old_fd, 0, 0);
}

/* dup3(fd, newfd, flags): dup2 + flags。
 * 本内核只接受 flags==0（O_CLOEXEC 所需的 fd 标志位还没实现）；
 * 且 oldfd==newfd 时返回 -1（与 dup2 的 no-op 成功不同，这是 POSIX 的规定）。 */
static inline int dup3(int old_fd, int new_fd, int flags) {
    return syscall3(SYS_DUP3, (usize)old_fd, (usize)new_fd, (usize)flags);
}
#ifdef __cplusplus
}
#endif
