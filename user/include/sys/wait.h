/*
 * sys/wait.h —— wait / waitpid 的状态宏
 *
 * 实现都在 process.h（waitpid 由 syscall 直接完成），这里只做 POSIX 归口。
 *
 * 注意本内核的 wait status 编码与 Linux 不同：
 *   内核把"被信号杀死"直接编码成退出码 128+sig（shell 的 $? 就是 139），
 *   而不是 Linux 那样把信号号放在低 7 位。所以 WIFEXITED / WIFSIGNALED
 *   按 exit_code 的大小判断（见 process.h 里的注释）。
 */
#ifndef RK_SYS_WAIT_H
#define RK_SYS_WAIT_H

#include "../process.h"
#include "types.h"

#endif /* RK_SYS_WAIT_H */
