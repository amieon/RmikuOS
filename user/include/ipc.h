#pragma once

/*
 * ipc.h —— 进程间通信层。
 *
 * 内容:
 *   - pipe(创建匿名管道,返回读端 / 写端 fd)
 *   - dup2(复制文件描述符,实现重定向)
 *
 * 这两个是 shell 管道 `cmd1 | cmd2` 与重定向 `> < ` 的底层支撑,
 * 从原 sys.h 拆出。只依赖 syscall.h。
 */

#include "syscall.h"

static inline int pipe(int fd[2]) {
    return syscall3(SYS_PIPE, (usize)fd, 0, 0);
}

static inline int dup2(int old_fd, int new_fd) {
    return syscall3(SYS_DUP2, (usize)old_fd, (usize)new_fd, 0);
}
