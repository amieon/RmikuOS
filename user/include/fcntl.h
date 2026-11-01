#ifndef RMIKU_FCNTL_H
#define RMIKU_FCNTL_H

/* POSIX <fcntl.h> —— 开放标志、fcntl 命令号，以及 fcntl() 本身。
 */

#include "flag.h"
#include "io.h"     /* rk_sys_ret：错误码必须过它翻成 -1 + errno */

#ifndef O_BINARY
#define O_BINARY 0     /* 无文本/二进制区分, POSIX 语义即 no-op */
#endif

/* fcntl 必须是变参：POSIX 里 F_GETFL / F_GETFD 这类"查询"命令只传两个参数，
 * 而 F_SETFL / F_SETFD / F_DUPFD 带第三个。固定三参版本会让
 *     fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
 */
static inline int fcntl(int fd, int cmd, ...) {
    va_list ap;
    int arg = 0;
    switch (cmd) {
    case F_GETFL:
    case F_GETFD:
        break;                     /* 查询类：没有第三个实参 */
    default:
        va_start(ap, cmd);
        arg = va_arg(ap, int);
        va_end(ap);
        break;
    }
    return (int)rk_sys_ret(syscall3(SYS_FCNTL, (usize)fd, (usize)cmd, (usize)arg));
}

#endif /* RMIKU_FCNTL_H */
