/* termios.h —— RmikuOS 最小 termios 模拟（kilo 编辑器等需要）。
 *
 * RmikuOS 终端是内核 line discipline（SYS_SET_ECHO 控制回显, UART 逐字符读）。
 * 没有完整的 termios/ICANON 行缓冲概念, 这里只模拟 kilo 用到的部分：
 *   - tcgetattr/tcsetattr: 只把 c_lflag 的 ECHO 位映射到 set_echo()（raw 模式 = 关回显）
 *   - VMIN/VTIME: RmikuOS read 天然逐字符, 忽略
 *   - 其余标志位: 定义供位运算使用, 无实际效果
 */
#ifndef RMIKU_TERMIOS_H
#define RMIKU_TERMIOS_H

typedef unsigned int  tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int  speed_t;

#define NCCS 32

struct termios {
    tcflag_t c_iflag;   /* input modes */
    tcflag_t c_oflag;   /* output modes */
    tcflag_t c_cflag;   /* control modes */
    tcflag_t c_lflag;   /* local modes */
    cc_t     c_cc[NCCS];
};

/* c_iflag */
#define BRKINT 0x0001
#define ICRNL  0x0002
#define INPCK  0x0004
#define ISTRIP 0x0008
#define IXON   0x0010
/* c_oflag */
#define OPOST  0x0001   /* 输出后处理——RmikuOS 无实际效果, 定义供位运算 */
/* c_cflag */
#define CS8    0x0030   /* 8 位字符——RmikuOS 固定, 定义供位运算 */
/* c_lflag */
#define ISIG   0x0001
#define ICANON 0x0002
#define ECHO   0x0008
#define IEXTEN 0x0004
/* c_cc indices */
#define VMIN   6
#define VTIME  5
/* tcsetattr opt */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* winsize: 给 ioctl(TIOCGWINSZ) 用（RmikuOS 返回失败, 调用方走 fallback） */
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#include "io.h"   /* set_echo */

static inline int tcgetattr(int fd, struct termios *t) {
    (void)fd;
    if (t) {
        memset(t, 0, sizeof(*t));
        t->c_lflag = ECHO;      /* 默认回显开（RmikuOS 默认） */
        t->c_cc[VMIN] = 1;
    }
    return 0;
}

static inline int tcsetattr(int fd, int opt, const struct termios *t) {
    (void)fd;
    (void)opt;
    if (t) {
        set_echo((t->c_lflag & ECHO) ? 1 : 0);   /* raw 模式 = 关回显 */
    }
    return 0;
}

#endif /* RMIKU_TERMIOS_H */
