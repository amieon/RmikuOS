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
#ifdef __cplusplus
}
#endif
