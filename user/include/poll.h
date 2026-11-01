/*
 * poll.h —— POSIX 风格 poll() 接口(用户态)
 *
 * 内核 ABI 与 struct pollfd 完全同布局:
 *   fd      i32
 *   events  i16(用户要听什么)
 *   revents i16(内核回填实际就绪)
 *
 * timeout: 毫秒;0 表示立即返回;-1 表示无限等待。
 * 当前支持普通文件/目录、管道、TCP/UDP/RAW socket;stdin 尚未提供 peek,
 * 暂不会把终端输入报成 POLLIN。
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"

typedef unsigned int nfds_t;

struct pollfd {
    int   fd;
    short events;
    short revents;
};

#define POLLIN   0x0001  /* 有数据可读,或读侧 EOF */
#define POLLPRI  0x0002  /* 紧急/带外数据可读。**本内核从不上报这一位**
                          * （TCP 带外数据未实现),但 POSIX 要求它存在:
                          * 大量移植代码用 `#define POLLRDBAND POLLPRI` 做兜底,
                          * 缺一个符号就整段编不过(curl 的 lib/select.h 就是这样)。 */
#define POLLOUT  0x0004  /* 至少还能写入一个字节 */
#define POLLERR  0x0008  /* 错误,不随 events 过滤 */
#define POLLHUP  0x0010  /* 对端挂断,不随 events 过滤 */
#define POLLNVAL 0x0020  /* fd 无效,不随 events 过滤 */

static inline int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    if (timeout < -1) {
        return -1;
    }
    /* -1 映射成 usize::MAX;其余非负值就是毫秒数。 */
    return (int)syscall3(SYS_POLL, (usize)fds, (usize)nfds, (usize)timeout);
}

#ifdef __cplusplus
}
#endif
