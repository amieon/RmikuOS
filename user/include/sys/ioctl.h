/* sys/ioctl.h —— RmikuOS 最小 ioctl 模拟。
 * RmikuOS 无 ioctl; TIOCGWINSZ(终端尺寸查询)返回失败, 调用方(如 kilo)走 fallback。 */
#ifndef RMIKU_SYS_IOCTL_H
#define RMIKU_SYS_IOCTL_H

#include "../termios.h"   /* struct winsize */

#define TIOCGWINSZ 0x5413

static inline int ioctl(int fd, unsigned long req, void *arg) {
    (void)fd;
    (void)req;
    (void)arg;
    return -1;   /* 不支持, 调用方应 fallback */
}

#endif /* RMIKU_SYS_IOCTL_H */
